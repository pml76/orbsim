#include "tests/FixtureFile.hpp"

#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace orb::test {

namespace {

// What separates words on a line. A CR is taken off with the line ending
// instead, so that it can never be read as part of a number.
constexpr std::string_view kBlank = " \t";

// How many decimal places a kilometre is from a metre, and a km/s from a m/s:
// the exponent parseScaled() moves, rather than a factor anything multiplies.
constexpr int kKilometresToMetres = 3;

// A failure, by name and line. One line at every call site rather than four.
[[nodiscard]] std::unexpected<FixtureError> refused(FixtureErrorKind kind, std::size_t line) {
    return std::unexpected(FixtureError{.kind = kind, .line = line});
}

[[nodiscard]] std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(kBlank);
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(kBlank);
    return text.substr(first, last - first + 1);
}

[[nodiscard]] std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> found;
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t start = text.find_first_not_of(kBlank, position);
        if (start == std::string_view::npos) break;
        const std::size_t end = text.find_first_of(kBlank, start);
        found.emplace_back(text.substr(start, end - start));
        if (end == std::string_view::npos) break;
        position = end;
    }
    return found;
}

// A finite number, read the way main.cpp reads one: std::from_chars, which is
// correctly rounded and ignores the locale. It also reads "nan" and "inf",
// which are refused here, because neither is a reference value.
[[nodiscard]] std::optional<f64> parseFinite(std::string_view text) {
    if (text.empty()) return std::nullopt;
    f64 value = 0.0;
    // std::to_address rather than data() + size(), as in main.cpp: the same
    // pointers, without arithmetic the compiler cannot bound.
    const char* const last = std::to_address(text.end());
    const auto [end, ec] = std::from_chars(std::to_address(text.begin()), last, value);
    if (ec != std::errc{} || end != last || !isFinite(value)) return std::nullopt;
    return value;
}

[[nodiscard]] std::optional<std::int64_t> parseExponent(std::string_view text) {
    // from_chars takes no leading '+' on an integer, and every exponent
    // Horizons writes has one.
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    std::int64_t value = 0;
    const char* const last = std::to_address(text.end());
    const auto [end, ec] = std::from_chars(std::to_address(text.begin()), last, value);
    if (ec != std::errc{} || end != last) return std::nullopt;
    return value;
}

// A Julian date read in two parts where the text allows it: the whole days,
// and the fraction as a number of its own. Read as one double, 2451910.123456789
// would be rounded to the ulp of a seven-digit day number -- 40 us -- before the
// time type ever saw it; split, each part is as exact as its own digits.
// Exponent notation, which the converter never writes, is read whole.
[[nodiscard]] std::optional<TdbTime> epochOf(std::string_view text) {
    const std::size_t point = text.find('.');
    const bool plainDecimal = text.find_first_of("eE") == std::string_view::npos;
    JulianDate date{};
    if (plainDecimal && point != std::string_view::npos) {
        const auto days = parseFinite(text.substr(0, point));
        const std::string fraction =
            std::string{text.front() == '-' ? "-0" : "0"} + std::string{text.substr(point)};
        const auto part = parseFinite(fraction);
        if (!days || !part) return std::nullopt;
        date = {.day = *days, .fraction = *part};
    } else {
        const auto whole = parseFinite(text);
        if (!whole) return std::nullopt;
        date = {.day = *whole, .fraction = 0.0};
    }
    const auto instant = TdbTime::fromJulianDate(date);
    if (!instant) return std::nullopt;
    return *instant;
}

// One `key = value` line, into the table's header.
[[nodiscard]] std::expected<void, FixtureError>
addHeaderLine(std::string_view line, std::size_t lineNumber, FixtureTable& table) {
    const std::size_t equals = line.find('=');
    const std::string_view key = trimmed(line.substr(0, equals));
    const std::string_view value = trimmed(line.substr(equals + 1));
    if (key.empty()) {
        return refused(FixtureErrorKind::MissingHeaderKey, lineNumber);
    }
    // A key given twice has two values, and at most one of them is what was
    // meant; neither wins silently.
    if (headerValue(table.header, key)) {
        return refused(FixtureErrorKind::UnexpectedHeaderValue, lineNumber);
    }
    table.header.push_back(
        HeaderLine{.key = std::string{key}, .value = std::string{value}, .line = lineNumber});
    return {};
}

// One data row, checked for width and for every field being a number.
[[nodiscard]] std::expected<void, FixtureError>
addDataRow(std::string_view line, std::size_t lineNumber, FixtureTable& table) {
    std::vector<std::string> fields = words(line);
    if (fields.size() != table.columns.size()) {
        return refused(FixtureErrorKind::WrongColumnCount, lineNumber);
    }
    for (const std::string& field : fields) {
        if (!parseFinite(field)) {
            return refused(FixtureErrorKind::NonNumericField, lineNumber);
        }
    }
    table.rows.push_back(FixtureRow{.line = lineNumber, .fields = std::move(fields)});
    return {};
}

// 1-based number of the last line of a text that does not end in a newline.
[[nodiscard]] std::size_t lastLineNumber(std::string_view text) {
    std::size_t newlines = 0;
    for (const char c : text) {
        if (c == '\n') ++newlines;
    }
    return newlines + 1;
}

// A header check of a typed reader: the key must be there, with exactly this
// value.
struct Required {
    std::string_view key;
    std::string_view value;
};

[[nodiscard]] std::expected<void, FixtureError> require(const FixtureTable& table,
                                                        Required required) {
    const auto found = std::ranges::find(table.header, required.key, &HeaderLine::key);
    if (found == table.header.end()) return refused(FixtureErrorKind::MissingHeaderKey, 0);
    if (found->value != required.value) {
        return refused(FixtureErrorKind::UnexpectedHeaderValue, found->line);
    }
    return {};
}

// One row of a state-vector table, in the simulation's units.
[[nodiscard]] std::expected<StateAtEpoch, FixtureError> stateOf(const FixtureRow& row) {
    const auto epoch = epochOf(row.fields.at(0));
    if (!epoch) {
        return refused(FixtureErrorKind::InvalidEpoch, row.line);
    }
    // Every field is already known to be a finite number; what can still fail
    // is a value that overflows once it is in metres.
    std::array<f64, 6> si{};
    for (std::size_t i = 0; i < si.size(); ++i) {
        const auto scaled = parseScaled(row.fields.at(i + 1), kKilometresToMetres);
        if (!scaled) {
            return refused(FixtureErrorKind::NonNumericField, row.line);
        }
        si.at(i) = *scaled;
    }
    const auto [x, y, z, vx, vy, vz] = si;
    return StateAtEpoch{
        .epoch = *epoch,
        .position = Position{Metres{x}, Metres{y}, Metres{z}},
        .velocity = Velocity{MetresPerSecond{vx}, MetresPerSecond{vy}, MetresPerSecond{vz}},
    };
}

} // namespace

std::optional<std::string_view> headerValue(const FixtureHeader& header, std::string_view key) {
    const auto found = std::ranges::find(header, key, &HeaderLine::key);
    if (found == header.end()) return std::nullopt;
    return std::string_view{found->value};
}

std::expected<FixtureTable, FixtureError> parseFixtureTable(std::string_view text) {
    if (text.empty()) {
        return refused(FixtureErrorKind::EmptyFile, 0);
    }
    // Checked before anything is read: a file cut off in the middle of a
    // number would otherwise parse, as a shorter number.
    if (text.back() != '\n') {
        return refused(FixtureErrorKind::TruncatedFile, lastLineNumber(text));
    }

    FixtureTable table;
    bool inData = false;
    std::size_t lineNumber = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start); // never npos: the text ends in one
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++lineNumber;
        if (line.ends_with('\r')) line.remove_suffix(1);
        line = trimmed(line);
        if (line.empty() || line.starts_with('#')) continue;

        // Everything before the first data row is header. After it, a line with
        // an '=' is simply a malformed row, and is reported as one.
        if (!inData && line.contains('=')) {
            if (const auto added = addHeaderLine(line, lineNumber, table); !added) {
                return std::unexpected(added.error());
            }
            continue;
        }
        if (!inData) {
            const auto columns = headerValue(table.header, "columns");
            if (!columns) {
                return refused(FixtureErrorKind::MissingHeaderKey, 0);
            }
            table.columns = words(*columns);
            inData = true;
        }
        if (const auto added = addDataRow(line, lineNumber, table); !added) {
            return std::unexpected(added.error());
        }
    }
    if (table.rows.empty()) {
        return refused(FixtureErrorKind::EmptyFile, 0);
    }
    return table;
}

std::expected<std::string, FixtureError> readFixtureText(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return refused(FixtureErrorKind::FileNotFound, 0);
    }
    // Binary, so that the bytes read are the bytes the checksum was taken over,
    // and a CRLF arrives as one for the parser to deal with.
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return refused(FixtureErrorKind::FileNotFound, 0);
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::optional<f64> parseScaled(std::string_view text, int powerOfTen) {
    // Refused before it is edited: the edit below must only ever see a finite
    // decimal number.
    if (!parseFinite(text)) return std::nullopt;
    const std::size_t e = text.find_first_of("eE");
    const std::string_view mantissa = text.substr(0, e);
    std::int64_t exponent = 0;
    if (e != std::string_view::npos) {
        const auto parsed = parseExponent(text.substr(e + 1));
        if (!parsed) return std::nullopt;
        exponent = *parsed;
    }
    // An int64 so that no exponent a double can carry, plus the shift, can
    // overflow the sum.
    return parseFinite(std::format("{}e{}", mantissa, exponent + powerOfTen));
}

std::expected<StateVectorFixture, FixtureError> parseStateVectors(std::string_view text) {
    const auto table = parseFixtureTable(text);
    if (!table) return std::unexpected(table.error());

    for (const Required required : {
             Required{.key = "frame", .value = kStateVectorFrame},
             Required{.key = "time scale", .value = kStateVectorTimeScale},
             Required{.key = "corrections", .value = kStateVectorCorrections},
         }) {
        if (const auto held = require(*table, required); !held) {
            return std::unexpected(held.error());
        }
    }
    // Compared as words rather than as text, so that aligning the header with
    // extra spaces is not a different set of columns.
    if (table->columns != words(kStateVectorColumns)) {
        // The columns key exists: parseFixtureTable() refuses a table without it.
        const auto columns = std::ranges::find(table->header, "columns", &HeaderLine::key);
        return refused(FixtureErrorKind::UnexpectedHeaderValue,
                       columns == table->header.end() ? 0 : columns->line);
    }

    StateVectorFixture fixture{.header = table->header, .states = {}};
    fixture.states.reserve(table->rows.size());
    for (const FixtureRow& row : table->rows) {
        const auto state = stateOf(row);
        if (!state) return std::unexpected(state.error());
        fixture.states.push_back(*state);
    }
    return fixture;
}

std::expected<StateVectorFixture, FixtureError>
readStateVectors(const std::filesystem::path& path) {
    const auto text = readFixtureText(path);
    if (!text) return std::unexpected(text.error());
    return parseStateVectors(*text);
}

std::string_view dataDirectory() { return ORBSIM_DATA_DIR; }

} // namespace orb::test
