#ifndef ORBSIM_TESTS_FIXTUREFILE_HPP
#define ORBSIM_TESTS_FIXTUREFILE_HPP
//
// Reference data this project did not produce, read with its provenance
// attached (M1-06, VERIFICATION.md rule 3).
//
// **The format** is this project's own, plain text, so that a diff is readable
// and a wrong number is visible in review:
//
//     # comments, anywhere, and blank lines
//     source      = JPL Horizons API
//     frame       = ICRF
//     columns     = jd_tdb x_km y_km z_km vx_km_s vy_km_s vz_km_s
//     2451545.000000000 2.649903367743050E+07 ...
//
// `key = value` lines first -- the header, which is the provenance, because the
// files themselves are not in the repository -- then one row per line of
// whitespace-separated decimal numbers, as many as `columns` names. LF or
// CRLF, and the last line must end in a newline: a file that stops short of
// one was cut off, which is how a download through a proxy fails. Horizons
// output is turned into this by scripts/horizons-fixture.py (decided
// 2026-09-19, register decision 42); M1-05's TDB - TT reference values are
// written in it by scripts/skyfield-fixture.py (decision 55), and M1-68's GMAT
// trajectory is to arrive in it too.
//
// **Two layers, and units at the first moment a number exists** (decided
// 2026-09-19). parseFixtureTable() checks the file's shape and that every
// field is a finite number, but keeps each field as the characters the file
// holds: no bare f64 ever sits in it. The typed readers above it --
// parseStateVectors() is the first -- turn text into TdbTime, metres and
// metres per second in one step. That is also what makes the conversion exact:
// kilometres become metres by moving the decimal exponent and parsing once,
// which is correctly rounded, where parsing and then multiplying by 1000 would
// round twice.
//
// **Every failure is reported by name, with the line it happened on**, rather
// than thrown or handed back as a half-read table.
//
#include "core/Math.hpp"
#include "core/Time.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orb::test {

// --- errors ------------------------------------------------------------------

// One name per way a fixture can be wrong, so a test asking for one by name
// proves the check for that failure is the one that fired (M1-06; register
// decisions 45 and 46).
enum class FixtureErrorKind : std::uint8_t {
    FileNotFound,          // no file at that path
    EmptyFile,             // no data rows: nothing at all, or a header alone
    TruncatedFile,         // the last line does not end in a newline
    MissingHeaderKey,      // a key the reader needs is not in the header
    UnexpectedHeaderValue, // a key is present with a value this reader cannot use
    WrongColumnCount,      // a row has more or fewer fields than `columns` names
    NonNumericField,       // a field is not a finite decimal number
    InvalidEpoch,          // an epoch that is a number, but not an instant
};

[[nodiscard]] constexpr std::string_view describe(FixtureErrorKind kind) noexcept {
    switch (kind) {
    case FixtureErrorKind::FileNotFound:
        return "there is no file at that path";
    case FixtureErrorKind::EmptyFile:
        return "the file holds no data rows";
    case FixtureErrorKind::TruncatedFile:
        return "the last line does not end in a newline, so the file was cut off";
    case FixtureErrorKind::MissingHeaderKey:
        return "a header key this reader needs is missing";
    case FixtureErrorKind::UnexpectedHeaderValue:
        return "a header key has a value this reader cannot use";
    case FixtureErrorKind::WrongColumnCount:
        return "a row has a different number of fields from the columns the header names";
    case FixtureErrorKind::NonNumericField:
        return "a field is not a finite decimal number";
    case FixtureErrorKind::InvalidEpoch:
        return "an epoch is a number but not an instant the time type can hold";
    }
    return "unknown fixture error";
}

// The kind, and where. A parser's failure without its line number is a
// failure somebody else cannot diagnose, which is the test VERIFICATION.md
// rule 1 sets for a message.
struct FixtureError {
    FixtureErrorKind kind{};
    std::size_t line{}; // 1-based; 0 for a failure that belongs to no one line
};

// Found by argument-dependent lookup, from errorName() in OrbitTestSupport.hpp.
[[nodiscard]] constexpr std::string_view describe(const FixtureError& error) noexcept {
    return describe(error.kind);
}

// --- the untyped layer ---------------------------------------------------------

// gcc's -Wabi-tag reports the three structs below: they hold std::string, and
// libstdc++ tags std::string with its "cxx11" ABI, which it wants every type
// holding one to carry too. The tag exists for code shipped as a binary and
// linked against the pre-C++11 string ABI; this project builds everything from
// source with one ABI, so the tag protects nothing here. Tagging them instead
// was measured on 2026-09-19 and spreads: gcc then reports every function that
// returns a tagged type. Off at this site alone, for gcc alone -- clang has no
// warning of that name and rejects one it does not know -- on the owner's
// ruling of 2026-09-19 (ADR 0017; register decision 52). The same mechanism
// OrbitTestSupport.hpp uses for Catch2's std::string.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
// One `key = value` line of a header, and the line it was on, so that a typed
// reader refusing a value can say where it is.
struct HeaderLine {
    std::string key;
    std::string value;
    std::size_t line{};

    [[nodiscard]] friend bool operator==(const HeaderLine&, const HeaderLine&) = default;
};

// A vector, not a map. A header is a handful of lines and a lookup is a short
// scan -- and std::map's move constructor allocates under the MSVC library, so
// it may throw, which bugprone-exception-escape rightly reports on every struct
// that holds one: a move is expected not to.
using FixtureHeader = std::vector<HeaderLine>;

// The value `key` has in a header, if it has one. The view is into the
// header, and lives as long as it does.
[[nodiscard]] std::optional<std::string_view> headerValue(const FixtureHeader& header,
                                                          std::string_view key);

// One data row: the line it came from, and its fields as the file spells them.
struct FixtureRow {
    std::size_t line{};
    std::vector<std::string> fields;
};

// A fixture file checked for shape, and nothing more. Every field is known to
// be a finite decimal number, and is still text.
struct FixtureTable {
    FixtureHeader header;
    std::vector<std::string> columns;
    std::vector<FixtureRow> rows;
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

[[nodiscard]] std::expected<FixtureTable, FixtureError> parseFixtureTable(std::string_view text);

// The whole file as text, or FileNotFound.
[[nodiscard]] std::expected<std::string, FixtureError>
readFixtureText(const std::filesystem::path& path);

// The number a field spells, times 10^powerOfTen, correctly rounded: the
// decimal exponent is moved and the text parsed once. Nothing for text that is
// not a finite decimal number, or whose scaled value is not finite.
//
// Two parameters of different types, and the second is an exponent, not a
// quantity: it says how many decimal places the unit moves -- 3 for km to m.
[[nodiscard]] std::optional<f64> parseScaled(std::string_view text, int powerOfTen);

// --- state vectors ---------------------------------------------------------

// One row of a state-vector fixture, in the units the simulation uses.
struct StateAtEpoch {
    TdbTime epoch;
    Position position;
    Velocity velocity;
};

struct StateVectorFixture {
    FixtureHeader header;
    std::vector<StateAtEpoch> states;
};

// The header a state-vector fixture must carry, and the only values this
// reader accepts. Anything else is refused by name rather than read in the
// wrong frame, the wrong scale or the wrong unit -- a file in AU read as km is
// wrong by a factor of 1.5e8 and still plausible to look at.
inline constexpr std::string_view kStateVectorFrame = "ICRF";
inline constexpr std::string_view kStateVectorTimeScale = "TDB";
inline constexpr std::string_view kStateVectorCorrections = "none";
inline constexpr std::string_view kStateVectorColumns =
    "jd_tdb x_km y_km z_km vx_km_s vy_km_s vz_km_s";

[[nodiscard]] std::expected<StateVectorFixture, FixtureError>
parseStateVectors(std::string_view text);

[[nodiscard]] std::expected<StateVectorFixture, FixtureError>
readStateVectors(const std::filesystem::path& path);

// --- TDB - TT ----------------------------------------------------------------

// One row of a TDB - TT fixture (M1-05): an instant in TDB, and how far TDB is
// ahead of TT at it.
struct TdbMinusTtAtEpoch {
    TdbTime epoch;
    Seconds tdbMinusTt;
};

struct TdbMinusTtFixture {
    FixtureHeader header;
    std::vector<TdbMinusTtAtEpoch> rows;
};

// The header a TDB - TT fixture must carry. The epoch is TDB because that is
// the argument Skyfield's tdb_minus_tt takes, and the difference is in seconds:
// read as milliseconds, a value of 1.6 ms would be a 1.6 s claim, still
// plausible-looking to anything that does not check its unit.
inline constexpr std::string_view kTdbMinusTtTimeScale = "TDB";
inline constexpr std::string_view kTdbMinusTtColumns = "jd_tdb tdb_minus_tt_s";

[[nodiscard]] std::expected<TdbMinusTtFixture, FixtureError> parseTdbMinusTt(std::string_view text);

[[nodiscard]] std::expected<TdbMinusTtFixture, FixtureError>
readTdbMinusTt(const std::filesystem::path& path);

// data/ in the source tree. The build names it, as it names ORBSIM_ASSET_DIR
// for the application.
//
// Its text rather than a std::filesystem::path: libstdc++ tags path with its
// "cxx11" ABI, and a function returning one draws gcc's -Wabi-tag. A view of
// the string literal the build supplies -- static storage, so it never
// dangles -- avoids that without switching anything off, and a caller builds
// the path from it at no cost.
[[nodiscard]] std::string_view dataDirectory();

} // namespace orb::test

#endif // ORBSIM_TESTS_FIXTUREFILE_HPP
