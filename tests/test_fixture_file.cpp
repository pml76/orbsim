//
// Tests for tests/FixtureFile.hpp: reference data read with its provenance
// attached (M1-06).
//
// Nothing here is checked against the code it tests. The expected values come
// from places independent of FixtureFile.cpp:
//
//   * the compiler's own reading of a floating literal, which is correctly
//     rounded and shares nothing with std::from_chars or with the exponent
//     edit the reader does -- `2.649903367743050e10` in this file is the
//     metres the reader must produce from `2.649903367743050E+07` km;
//   * exact rational arithmetic -- Python's fractions module -- for three
//     values on which parsing and *then* multiplying by 1000 gives a different
//     double from the correctly rounded one, so that the test fails if the
//     exponent edit is ever "simplified" into a multiply. Found 2026-09-19:
//     three of the first eight Horizons-shaped values searched;
//   * std::format's round-trip guarantee, for the claim that a value written
//     with 17 significant digits reads back bit for bit;
//   * the calendar, for epochs: J2000.0 is 2000-01-01T12:00 whichever way it
//     is reached, and the reader reaches it from a Julian date.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "tests/FixtureFile.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <string_view>

using namespace orb;
using namespace orb::test;

// A note on the readability-function-cognitive-complexity suppressions below:
// Catch2's REQUIRE expands to a do-while around a try/catch, so a case scores
// about three points per assertion whether or not it branches. Ruled by the
// project owner on 2026-09-09 -- see tests/test_orbit_scales.cpp -- one
// suppression per function, and only where the check fires.

namespace {

// A complete, well-formed state-vector fixture, written the way the converter
// writes one. Two rows; the first is J2000.0, with Horizons' own digits for
// the geocentric Sun at that instant.
constexpr std::string_view kGoodFixture =
    "# orbsim fixture: state vectors\n"
    "source      = JPL Horizons API\n"
    "ephemeris   = DE441\n"
    "target      = Sun (10)\n"
    "center      = Earth (399)\n"
    "frame       = ICRF\n"
    "corrections = none\n"
    "time scale  = TDB\n"
    "columns     = jd_tdb x_km y_km z_km vx_km_s vy_km_s vz_km_s\n"
    "#\n"
    "\n"
    "2451545.000000000 2.649903367743050E+07 -1.327574173383451E+08 "
    "-5.755671847054072E+07 2.979426007043741E+01 5.018052308799903E+00 "
    "2.175393802830554E+00\n"
    "2451910.000000000 2.584569461634472E+07 -1.328651156764532E+08 "
    "-5.760410185130302E+07 2.981426778049664E+01 4.913369305044703E+00 "
    "2.129569190710212E+00\n";

// One header line of kGoodFixture and what replaces it, as one parameter: two
// adjacent string_views transpose in silence, and a transposed pair here would
// search for the replacement and test nothing.
struct HeaderEdit {
    std::string_view key;
    std::string_view replacement;
};

// The same file with one header line replaced. Built by search rather than by
// position, so that a reordering of kGoodFixture cannot quietly make a case
// test something else.
[[nodiscard]] std::string withHeaderLine(HeaderEdit edit) {
    std::string text{kGoodFixture};
    const std::string prefix = std::string{edit.key} + " ";
    const std::size_t start = text.find(prefix);
    REQUIRE(start != std::string::npos);
    const std::size_t end = text.find('\n', start);
    text.replace(start, end - start, edit.replacement);
    return text;
}

// A refusal, asked for by name and by line.
struct Refusal {
    std::string_view name;
    std::string text;
    FixtureErrorKind kind;
    std::size_t line;
};

// What an unread value is taken as. Its bits equal no finite value's, so a
// comparison against an expected finite value fails if the read did -- and it
// lets the comparison use value_or(), which bugprone-unchecked-optional-access
// accepts, where it cannot see that the REQUIRE before a value() returned.
constexpr f64 kNotRead = std::numeric_limits<f64>::quiet_NaN();

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260919ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 10'000;

} // namespace

// --- the untyped layer --------------------------------------------------------

// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a known-good fixture parses, header and rows", "[fixture]") {
    const auto table = parseFixtureTable(kGoodFixture);
    INFO(errorName(table));
    REQUIRE(table.has_value());

    REQUIRE(table->header.size() == 8);
    REQUIRE(headerValue(table->header, "source") == "JPL Horizons API");
    REQUIRE(headerValue(table->header, "time scale") == "TDB"); // a key may hold a space
    REQUIRE(headerValue(table->header, "frame") == "ICRF");

    REQUIRE(table->columns.size() == 7);
    REQUIRE(table->columns.front() == "jd_tdb");
    REQUIRE(table->columns.back() == "vz_km_s");

    REQUIRE(table->rows.size() == 2);
    REQUIRE(table->rows.front().line == 12);
    REQUIRE(table->rows.back().line == 13);
    REQUIRE(table->rows.front().fields.size() == 7);
    // Still text, exactly as the file spells it.
    REQUIRE(table->rows.front().fields.at(1) == "2.649903367743050E+07");
}

// Comments, blank lines and CRLF line endings are all accepted, and change
// nothing about what is read.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("comments, blank lines and CRLF change nothing", "[fixture]") {
    std::string crlf;
    for (const char c : kGoodFixture) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const auto lf = parseFixtureTable(kGoodFixture);
    const auto windows = parseFixtureTable(crlf);
    REQUIRE(lf.has_value());
    INFO(errorName(windows));
    REQUIRE(windows.has_value());
    // Not vacuous: two empty parses would agree on everything below. Seen to
    // happen against the stub, where this case then aborted on front() rather
    // than failing with a message.
    REQUIRE(lf->header.size() == 8);
    REQUIRE(lf->rows.size() == 2);
    REQUIRE(windows->header == lf->header);
    REQUIRE(windows->rows.size() == lf->rows.size());
    REQUIRE(windows->rows.front().fields == lf->rows.front().fields);
}

// Every way a fixture can be wrong, each asked for by the name of the check
// that must catch it, and at the line it is on.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("every malformed fixture is refused by name and line", "[fixture][errors]") {
    const std::string good{kGoodFixture};
    const std::array refusals = std::to_array<Refusal>({
        {.name = "an empty file", .text = "", .kind = FixtureErrorKind::EmptyFile, .line = 0},
        {
            .name = "a header and no rows",
            .text = "columns = a b\n",
            .kind = FixtureErrorKind::EmptyFile,
            .line = 0,
        },
        {
            .name = "comments and nothing else",
            .text = "# nothing here\n\n",
            .kind = FixtureErrorKind::EmptyFile,
            .line = 0,
        },
        {
            .name = "a truncated final line",
            .text = good.substr(0, good.size() - 5),
            .kind = FixtureErrorKind::TruncatedFile,
            .line = 13,
        },
        {
            .name = "no columns key",
            .text = "frame = ICRF\n1 2\n",
            .kind = FixtureErrorKind::MissingHeaderKey,
            .line = 0,
        },
        {
            .name = "a header line with no key",
            .text = "= ICRF\ncolumns = a\n1\n",
            .kind = FixtureErrorKind::MissingHeaderKey,
            .line = 1,
        },
        {
            .name = "a key given twice",
            .text = "frame = ICRF\nframe = ICRF\ncolumns = a\n1\n",
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 2,
        },
        {
            .name = "a row with too few columns",
            .text = "columns = a b c\n1 2 3\n4 5\n",
            .kind = FixtureErrorKind::WrongColumnCount,
            .line = 3,
        },
        {
            .name = "a row with too many columns",
            .text = "columns = a b\n1 2 3\n",
            .kind = FixtureErrorKind::WrongColumnCount,
            .line = 2,
        },
        {
            .name = "a word where a number should be",
            .text = "columns = a b\n1 two\n",
            .kind = FixtureErrorKind::NonNumericField,
            .line = 2,
        },
        {
            .name = "a number with trailing characters",
            .text = "columns = a\n1.5x\n",
            .kind = FixtureErrorKind::NonNumericField,
            .line = 2,
        },
        // from_chars reads these three as doubles; none is a reference value.
        {
            .name = "nan",
            .text = "columns = a\nnan\n",
            .kind = FixtureErrorKind::NonNumericField,
            .line = 2,
        },
        {
            .name = "inf",
            .text = "columns = a\ninf\n",
            .kind = FixtureErrorKind::NonNumericField,
            .line = 2,
        },
        {
            .name = "a number too large for a double",
            .text = "columns = a\n1e400\n",
            .kind = FixtureErrorKind::NonNumericField,
            .line = 2,
        },
        {
            .name = "a header line after the data began",
            .text = "columns = a\n1\nframe = ICRF\n",
            .kind = FixtureErrorKind::WrongColumnCount,
            .line = 3,
        },
    });

    for (const Refusal& refusal : refusals) {
        const auto table = parseFixtureTable(refusal.text);
        INFO(refusal.name << " -> " << errorName(table));
        REQUIRE(!table.has_value());
        REQUIRE(table.error().kind == refusal.kind);
        REQUIRE(table.error().line == refusal.line);
    }
}

TEST_CASE("a file that is not there is FileNotFound", "[fixture][errors]") {
    const auto text = readFixtureText(std::filesystem::path{dataDirectory()} / "horizons" /
                                      "no-such-fixture.txt");
    INFO(errorName(text));
    REQUIRE(!text.has_value());
    REQUIRE(text.error().kind == FixtureErrorKind::FileNotFound);
    REQUIRE(text.error().line == 0);
}

TEST_CASE("every fixture error describes itself", "[fixture][errors]") {
    constexpr std::array kKinds = std::to_array<FixtureErrorKind>({
        FixtureErrorKind::FileNotFound,
        FixtureErrorKind::EmptyFile,
        FixtureErrorKind::TruncatedFile,
        FixtureErrorKind::MissingHeaderKey,
        FixtureErrorKind::UnexpectedHeaderValue,
        FixtureErrorKind::WrongColumnCount,
        FixtureErrorKind::NonNumericField,
        FixtureErrorKind::InvalidEpoch,
    });
    for (std::size_t i = 0; i < kKinds.size(); ++i) {
        CAPTURE(i);
        REQUIRE(!describe(kKinds.at(i)).empty());
        for (std::size_t j = 0; j < i; ++j) {
            REQUIRE(describe(kKinds.at(i)) != describe(kKinds.at(j)));
        }
    }
}

// --- numbers ------------------------------------------------------------------

// Any double, written with 17 significant digits, reads back bit for bit --
// a reference that loses precision in its own reader quietly loosens every
// budget checked against it. std::format's shortest-round-trip guarantee is the
// independent half; drawing the bits directly covers every exponent and both
// signs, and 17 digits exercises both the plain and the exponent spellings.
TEST_CASE("a double written with 17 digits reads back bit for bit", "[fixture][numbers]") {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng{kSweepSeed};
    std::size_t checked = 0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const f64 value = std::bit_cast<f64>(rng());
        if (!isFinite(value)) continue;
        const std::string text = std::format("{:.17g}", value);
        CAPTURE(kSweepSeed, i, text);
        const auto read = parseScaled(text, 0);
        REQUIRE(read.has_value());
        REQUIRE(bitsOf(read.value_or(kNotRead)) == bitsOf(value));
        ++checked;
    }
    // Almost every 64-bit pattern is a finite double; a sweep that silently
    // skipped them all would prove nothing.
    REQUIRE(checked > kSweepCases * 9 / 10);
}

// Kilometres become metres by moving the decimal exponent and parsing once, so
// the result is the double nearest the true value in metres. The three values
// below are ones on which parsing *then* multiplying by 1000 gives the next
// double over; the expected results are exact rational arithmetic's.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("kilometres become metres with a single rounding", "[fixture][numbers]") {
    struct Case {
        std::string_view kilometres;
        f64 metres;
    };
    constexpr std::array kCases = std::to_array<Case>({
        {.kilometres = "-7.487146101232058E+00", .metres = -0x1.d3f2566e3ed98p+12},
        {.kilometres = "-8.885409869719771E+06", .metres = -0x1.08ce4c26dc217p+33},
        {.kilometres = "9.689293741126924E+00", .metres = 0x1.2eca5994f2ad0p+13},
    });
    for (const Case& c : kCases) {
        CAPTURE(c.kilometres);
        const auto read = parseScaled(c.kilometres, 3);
        REQUIRE(read.has_value());
        REQUIRE(bitsOf(read.value_or(kNotRead)) == bitsOf(c.metres));
    }

    // Every spelling of the exponent, and none at all.
    REQUIRE(bitsOf(parseScaled("1.5E+07", 3).value_or(0.0)) == bitsOf(1.5e10));
    REQUIRE(bitsOf(parseScaled("1.5e7", 3).value_or(0.0)) == bitsOf(1.5e10));
    REQUIRE(bitsOf(parseScaled("1.5E-07", 3).value_or(0.0)) == bitsOf(1.5e-4));
    REQUIRE(bitsOf(parseScaled("1.5", 3).value_or(0.0)) == bitsOf(1500.0));
    REQUIRE(bitsOf(parseScaled("-2", 3).value_or(0.0)) == bitsOf(-2000.0));

    // And refusals: not a number, not finite, or finite until it was scaled.
    REQUIRE(!parseScaled("two", 3).has_value());
    REQUIRE(!parseScaled("nan", 0).has_value());
    REQUIRE(!parseScaled("1e308", 3).has_value());
    REQUIRE(!parseScaled("+1.5", 0).has_value()); // a sign the converter never writes
    REQUIRE(!parseScaled("", 0).has_value());
}

// --- state vectors ----------------------------------------------------------

// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a state-vector fixture arrives in metres, metres per second and TDB",
          "[fixture][states]") {
    const auto fixture = parseStateVectors(kGoodFixture);
    INFO(errorName(fixture));
    REQUIRE(fixture.has_value());
    REQUIRE(fixture->states.size() == 2);
    REQUIRE(headerValue(fixture->header, "target") == "Sun (10)");

    const StateAtEpoch& first = fixture->states.front();

    // J2000.0, reached through the calendar rather than a Julian date.
    const auto j2000 = TdbTime::fromCalendar({.year = 2000, .month = 1, .day = 1, .hour = 12});
    REQUIRE(j2000.has_value());
    REQUIRE(first.epoch == *j2000);

    // The compiler's reading of each literal is the reference: it is correctly
    // rounded and owes nothing to the reader.
    REQUIRE(first.position.x.bitIdentical(Metres{2.649903367743050e10}));
    REQUIRE(first.position.y.bitIdentical(Metres{-1.327574173383451e11}));
    REQUIRE(first.position.z.bitIdentical(Metres{-5.755671847054072e10}));
    REQUIRE(first.velocity.x.bitIdentical(MetresPerSecond{2.979426007043741e4}));
    REQUIRE(first.velocity.y.bitIdentical(MetresPerSecond{5.018052308799903e3}));
    REQUIRE(first.velocity.z.bitIdentical(MetresPerSecond{2.175393802830554e3}));

    // The second row is 365 days later, exactly.
    REQUIRE_THAT((fixture->states.back().epoch - first.epoch).value(),
                 WithinAbsOf(365.0 * 86'400.0, Tolerance{0.0}));
}

// A Julian date is read in two parts, the whole days and the fraction, so a
// fractional epoch is not rounded to the ulp of a seven-digit day number --
// 40 us at JD 2.45e6 -- before the time type ever sees it.
TEST_CASE("a fractional epoch keeps its fraction", "[fixture][states]") {
    std::string text{kGoodFixture};
    const std::size_t row = text.find("2451910.000000000");
    REQUIRE(row != std::string::npos);
    text.replace(row, 17, "2451910.123456789");
    const auto fixture = parseStateVectors(text);
    INFO(errorName(fixture));
    REQUIRE(fixture.has_value());
    REQUIRE(fixture->states.size() == 2);
    const auto expected = TdbTime::fromJulianDate({.day = 2451910.0, .fraction = 0.123456789});
    REQUIRE(expected.has_value());
    REQUIRE(fixture->states.back().epoch == *expected);
}

// A state-vector fixture must say it is in the frame, the scale, the
// corrections and the units this reader converts from. Anything else is
// refused by name rather than read wrongly: a file in AU read as km is out by
// 1.5e8 and still plausible to look at (register decision 45).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a state-vector fixture in the wrong frame, scale or units is refused",
          "[fixture][states][errors]") {
    const std::array refusals = std::to_array<Refusal>({
        {
            .name = "ecliptic",
            .text = withHeaderLine({.key = "frame", .replacement = "frame = ECLIPJ2000"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 6,
        },
        {
            .name = "TT rather than TDB",
            .text = withHeaderLine({.key = "time scale", .replacement = "time scale = TT"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 8,
        },
        {
            .name = "light-time corrected",
            .text = withHeaderLine({.key = "corrections", .replacement = "corrections = LT"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 7,
        },
        {
            .name = "astronomical units",
            .text = withHeaderLine({
                .key = "columns",
                .replacement = "columns = jd_tdb x_au y_au z_au vx_au_d vy_au_d vz_au_d",
            }),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 9,
        },
        {
            .name = "no frame at all",
            .text = withHeaderLine({.key = "frame", .replacement = "# frame removed"}),
            .kind = FixtureErrorKind::MissingHeaderKey,
            .line = 0,
        },
        {
            .name = "an epoch past the calendar",
            .text =
                [] {
                    std::string t{kGoodFixture};
                    t.replace(t.find("2451910.000000000"), 17, "9999999999.00000");
                    return t;
                }(),
            .kind = FixtureErrorKind::InvalidEpoch,
            .line = 13,
        },
    });

    for (const Refusal& refusal : refusals) {
        const auto fixture = parseStateVectors(refusal.text);
        INFO(refusal.name << " -> " << errorName(fixture));
        REQUIRE(!fixture.has_value());
        REQUIRE(fixture.error().kind == refusal.kind);
        REQUIRE(fixture.error().line == refusal.line);
    }
}

// --- TDB - TT -------------------------------------------------------------------

namespace {

// The first three rows of data/skyfield/tdb-minus-tt.txt, under its header.
constexpr std::string_view kGoodTdbFixture =
    "# orbsim fixture: TDB - TT at the geocentre\n"
    "source      = Skyfield 1.55 (Brandon Rhodes, MIT licence)\n"
    "function    = skyfield.timelib.tdb_minus_tt\n"
    "model       = USNO Circular 179 (Kaplan 2005), eq. 2.6\n"
    "numpy       = 2.5.3\n"
    "time scale  = TDB\n"
    "columns     = jd_tdb tdb_minus_tt_s\n"
    "2415020.5 -1.841120030058693e-05\n"
    "2415057.5 0.000997346517151686\n"
    "2415094.5 0.0016089583859698765\n";

// kGoodTdbFixture with one header line replaced, by search, as withHeaderLine
// does for the state vectors.
[[nodiscard]] std::string withTdbHeaderLine(HeaderEdit edit) {
    std::string text{kGoodTdbFixture};
    const std::string prefix = std::string{edit.key} + " ";
    const std::size_t start = text.find(prefix);
    REQUIRE(start != std::string::npos);
    const std::size_t end = text.find('\n', start);
    text.replace(start, end - start, edit.replacement);
    return text;
}

} // namespace

// The epochs against the calendar, and the values against the compiler's own
// reading of each literal: correctly rounded, and owing nothing to the reader.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a TDB - TT fixture arrives in TDB and seconds", "[fixture][tdb]") {
    const auto fixture = parseTdbMinusTt(kGoodTdbFixture);
    INFO(errorName(fixture));
    REQUIRE(fixture.has_value());
    REQUIRE(fixture->rows.size() == 3);
    REQUIRE(headerValue(fixture->header, "function") == "skyfield.timelib.tdb_minus_tt");

    // JD 2415020.5 is 1900-01-01T00:00, and the rows are 37 days apart.
    const auto first = TdbTime::fromCalendar({.year = 1900, .month = 1, .day = 1});
    REQUIRE(first.has_value());
    REQUIRE(fixture->rows.front().epoch == *first);
    REQUIRE_THAT((fixture->rows.at(1).epoch - fixture->rows.front().epoch).value(),
                 WithinAbsOf(37.0 * 86'400.0, Tolerance{0.0}));

    REQUIRE(fixture->rows.at(0).tdbMinusTt.bitIdentical(Seconds{-1.841120030058693e-05}));
    REQUIRE(fixture->rows.at(1).tdbMinusTt.bitIdentical(Seconds{0.000997346517151686}));
    REQUIRE(fixture->rows.at(2).tdbMinusTt.bitIdentical(Seconds{0.0016089583859698765}));
}

// A TDB - TT fixture must be in the scale and the unit this reader converts
// from, and anything else is refused by name.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a TDB - TT fixture in the wrong scale or unit is refused", "[fixture][tdb][errors]") {
    const std::array refusals = std::to_array<Refusal>({
        {
            .name = "epochs in TT",
            .text = withTdbHeaderLine({.key = "time scale", .replacement = "time scale = TT"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 6,
        },
        {
            .name = "milliseconds",
            .text = withTdbHeaderLine(
                {.key = "columns", .replacement = "columns = jd_tdb tdb_minus_tt_ms"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 7,
        },
        {
            .name = "no time scale at all",
            .text = withTdbHeaderLine({.key = "time scale", .replacement = "# removed"}),
            .kind = FixtureErrorKind::MissingHeaderKey,
            .line = 0,
        },
        {
            .name = "an epoch past the calendar",
            .text =
                [] {
                    std::string t{kGoodTdbFixture};
                    t.replace(t.find("2415057.5"), 9, "9999999999.5");
                    return t;
                }(),
            .kind = FixtureErrorKind::InvalidEpoch,
            .line = 9,
        },
    });

    for (const Refusal& refusal : refusals) {
        const auto fixture = parseTdbMinusTt(refusal.text);
        INFO(refusal.name << " -> " << errorName(fixture));
        REQUIRE(!fixture.has_value());
        REQUIRE(fixture.error().kind == refusal.kind);
        REQUIRE(fixture.error().line == refusal.line);
    }
}

// The committed file itself. Not skipped when absent, unlike the Horizons
// fixtures: it is in the repository (register decision 55), so its absence is
// a broken checkout. A plausibility check on its shape and its unit, not an
// accuracy claim -- that is test_astro_time.cpp's, against these same rows.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the committed TDB - TT fixture reads, in seconds", "[fixture][tdb]") {
    const auto fixture =
        readTdbMinusTt(std::filesystem::path{dataDirectory()} / "skyfield" / "tdb-minus-tt.txt");
    INFO(errorName(fixture) << " at line " << (fixture ? std::size_t{0} : fixture.error().line));
    REQUIRE(fixture.has_value());
    REQUIRE(headerValue(fixture->header, "source") ==
            "Skyfield 1.55 (Brandon Rhodes, MIT licence)");
    REQUIRE(headerValue(fixture->header, "model") == "USNO Circular 179 (Kaplan 2005), eq. 2.6");

    // 1900-01-01 to 2100-01-01 on a 37-day stride: MJD 15020 to 88069 holds
    // floor((88069 - 15020) / 37) + 1 = 1975 epochs.
    REQUIRE(fixture->rows.size() == 1975);
    const auto first = TdbTime::fromCalendar({.year = 1900, .month = 1, .day = 1});
    REQUIRE(first.has_value());
    REQUIRE(fixture->rows.front().epoch == *first);
    for (std::size_t i = 1; i < fixture->rows.size(); ++i) {
        CAPTURE(i);
        REQUIRE_THAT((fixture->rows.at(i).epoch - fixture->rows.at(i - 1).epoch).value(),
                     WithinAbsOf(37.0 * 86'400.0, Tolerance{0.0}));
    }

    // In seconds: the term's amplitude is about 1.7 ms, so every value lies
    // inside 2 ms and the largest reaches past 1.5 ms. Milliseconds read as
    // seconds would put them near 1.7, and microseconds near 1e-9.
    f64 largest = 0.0;
    for (const TdbMinusTtAtEpoch& row : fixture->rows) {
        const f64 seconds = row.tdbMinusTt.value();
        CAPTURE(seconds);
        REQUIRE(absOf(seconds) < 2e-3);
        largest = std::max(largest, absOf(seconds));
    }
    CAPTURE(largest);
    REQUIRE(largest > 1.5e-3);
}

// --- the Sun fixture itself ---------------------------------------------------

// The file the error budgets will be measured against, if this machine has
// generated it. Its bytes are checked against the committed hash by a CTest
// test of its own (horizons_fixture_checksums); this checks that it reads, and
// that the reading came out in metres rather than kilometres.
//
// **Skipped loudly when absent**, never passed quietly: Horizons output is not
// redistributed, so a fresh clone does not have it, and a suite that silently
// stopped checking would be ADR 0005's complaint exactly.
// --- the Earth's orientation (M1-07) -------------------------------------------

namespace {

// The header of data/skyfield/earth-orientation.txt, and its first two rows.
constexpr std::string_view kGoodOrientationFixture =
    "# orbsim fixture: the rotation from the celestial frame to the terrestrial one\n"
    "source      = Skyfield 1.55 (Brandon Rhodes, MIT licence)\n"
    "function    = skyfield.framelib.itrs.rotation_at\n"
    "models      = IAU 2006 precession, IAU 2000A nutation, IERS 2003 frame bias\n"
    "route       = equinox based: R3(GAST) x N x P x B\n"
    "numpy       = 2.5.3\n"
    "frame       = ICRS to ITRS\n"
    "polar motion = none\n"
    "delta t     = 69.2138671875 s, held constant\n"
    "time scale  = TT and UT1\n"
    "columns     = jd_tt_day jd_tt_fraction jd_ut1_day jd_ut1_fraction c2t_11 c2t_12 c2t_13 "
    "c2t_21 c2t_22 c2t_23 c2t_31 c2t_32 c2t_33\n"
    "2415020.5 0.0 2415019.5 0.9991989135742188 "
    "-0.19380523262618962 0.9810384464698054 -0.0017602144238795104 "
    "-0.9809922303609004 -0.1938131894709629 -0.009523211571717058 "
    "-0.00968378945736312 -0.00011889156041587714 0.999953103943651\n"
    "2415091.5 0.6180648803710938 2415091.5 0.6172637939453125 "
    "0.8263893590332926 0.5630415523648832 0.008052179027377293 "
    "-0.5630161808252377 0.8264285794920545 -0.005346319080050962 "
    "-0.009664750669678679 -0.00011536588556995582 0.9999532885516229\n";

// The same, with one header line replaced.
//
// Anchored at the start of a line, unlike the two helpers above: this
// fixture's own comment line contains the word "frame", and a search for the
// key alone edited the comment and left the header it meant to change --
// which made a refusal test pass by not testing anything.
[[nodiscard]] std::string withOrientationHeaderLine(HeaderEdit edit) {
    std::string text{kGoodOrientationFixture};
    const std::string prefix = "\n" + std::string{edit.key} + " ";
    const std::size_t found = text.find(prefix);
    REQUIRE(found != std::string::npos);
    const std::size_t start = found + 1;
    const std::size_t end = text.find('\n', start);
    text.replace(start, end - start, edit.replacement);
    return text;
}

} // namespace

// The two instants against the calendar and against the literals themselves,
// and the nine elements against the compiler's own reading of each: the reader
// owes the test nothing.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("an Earth-orientation fixture arrives as two instants and a rotation",
          "[fixture][orientation]") {
    const auto fixture = parseEarthOrientation(kGoodOrientationFixture);
    INFO(errorName(fixture));
    REQUIRE(fixture.has_value());
    REQUIRE(fixture->rows.size() == 2);
    REQUIRE(headerValue(fixture->header, "function") == "skyfield.framelib.itrs.rotation_at");
    REQUIRE(headerValue(fixture->header, "polar motion") == "none");

    // JD 2415020.5 is 1900-01-01T00:00 exactly, and the UT1 of that row is
    // 69.2138671875 s earlier -- 23:58:50.7861328125 of 1899-12-31.
    const auto midnight = TtTime::fromCalendar({.year = 1900, .month = 1, .day = 1});
    REQUIRE(midnight.has_value());
    REQUIRE(fixture->rows.front().tt == *midnight);
    const auto ut1 = Ut1Time::fromCalendar({
        .year = 1899,
        .month = 12,
        .day = 31,
        .hour = 23,
        .minute = 58,
        .second = Seconds{50.7861328125},
    });
    REQUIRE(ut1.has_value());
    REQUIRE(fixture->rows.front().ut1 == *ut1);

    // Row-major, in the order the columns name: c2t_11 first, c2t_33 last.
    const RotationMatrix& m = fixture->rows.front().celestialToTerrestrial;
    REQUIRE_THAT(m.rows.at(0).at(0), WithinAbsOf(-0.19380523262618962, Tolerance{0.0}));
    REQUIRE_THAT(m.rows.at(0).at(2), WithinAbsOf(-0.0017602144238795104, Tolerance{0.0}));
    REQUIRE_THAT(m.rows.at(1).at(1), WithinAbsOf(-0.1938131894709629, Tolerance{0.0}));
    REQUIRE_THAT(m.rows.at(2).at(2), WithinAbsOf(0.999953103943651, Tolerance{0.0}));
    REQUIRE(isRotation(m, kRotationTolerance));

    // The second row's time of day is a whole number of 2^-19 days, which is
    // what keeps every instant exact: 0.6180648803710938 * 2^19 = 324044.
    // Divided before it is multiplied: a day is 2^19 x 164 794 921 875 ps
    // exactly, and the product the other way round overflows an int64.
    constexpr std::int64_t kUnitsPerDay = 524'288; // 2^19, as the generator divides the day
    constexpr std::int64_t kUnitOfDay = 86'400'000'000'000'000LL / kUnitsPerDay;
    static_assert(kUnitOfDay * kUnitsPerDay == 86'400'000'000'000'000LL, "2^-19 day, exactly");
    REQUIRE(fixture->rows.at(1).tt.picosecondOfDay() == 324'044LL * kUnitOfDay);
}

// The header decides what the nine numbers mean. A rotation read as though
// polar motion were in it, or between the wrong frames, is a plausible matrix
// and a wrong claim -- and the budget asserted against it is 0.1 mas, where
// polar motion alone is 600.
TEST_CASE("an Earth-orientation fixture in the wrong frame or scale is refused",
          "[fixture][orientation][errors]") {
    const std::array refusals = std::to_array<Refusal>({
        {
            .name = "polar motion applied",
            .text = withOrientationHeaderLine(
                {.key = "polar motion", .replacement = "polar motion = IERS EOP 20 C04"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 8,
        },
        {
            .name = "the other direction",
            .text = withOrientationHeaderLine(
                {.key = "frame", .replacement = "frame       = ITRS to ICRS"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 7,
        },
        {
            .name = "UTC where UT1 belongs",
            .text = withOrientationHeaderLine(
                {.key = "time scale", .replacement = "time scale  = TT and UTC"}),
            .kind = FixtureErrorKind::UnexpectedHeaderValue,
            .line = 10,
        },
        {
            .name = "no frame at all",
            .text = withOrientationHeaderLine({.key = "frame", .replacement = "# removed"}),
            .kind = FixtureErrorKind::MissingHeaderKey,
            .line = 0,
        },
        {
            .name = "a column missing",
            .text = withOrientationHeaderLine({
                .key = "columns",
                .replacement = "columns     = jd_tt_day jd_tt_fraction jd_ut1_day "
                               "jd_ut1_fraction c2t_11 c2t_12 c2t_13 c2t_21 c2t_22 c2t_23 "
                               "c2t_31 c2t_32",
            }),
            .kind = FixtureErrorKind::WrongColumnCount,
            // The first data row, which the header has just stopped
            // describing: twelve columns named, thirteen fields written.
            .line = 12,
        },
        {
            .name = "an epoch past the calendar",
            .text =
                [] {
                    std::string t{kGoodOrientationFixture};
                    t.replace(t.find("2415091.5"), 9, "9999999999.5");
                    return t;
                }(),
            .kind = FixtureErrorKind::InvalidEpoch,
            // The second data row: the header is eleven lines.
            .line = 13,
        },
    });

    for (const Refusal& refusal : refusals) {
        const auto fixture = parseEarthOrientation(refusal.text);
        INFO(refusal.name << " -> " << errorName(fixture));
        REQUIRE(!fixture.has_value());
        REQUIRE(fixture.error().kind == refusal.kind);
        REQUIRE(fixture.error().line == refusal.line);
    }
}

// The committed file itself: its shape, its stride, and that every row really
// is a rotation. The accuracy claim against it is
// tests/test_earth_orientation.cpp's.
// Catch2 macro expansion, not written complexity. See the note above.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the committed Earth-orientation fixture reads, as rotations", "[fixture][orientation]") {
    const auto fixture = readEarthOrientation(std::filesystem::path{dataDirectory()} / "skyfield" /
                                              "earth-orientation.txt");
    INFO(errorName(fixture) << " at line " << (fixture ? std::size_t{0} : fixture.error().line));
    REQUIRE(fixture.has_value());

    // 1900-01-01 to 2100-01-01 on a 71-day stride: MJD 15020 to 88069 holds
    // floor((88069 - 15020) / 71) + 1 = 1029 epochs.
    REQUIRE(fixture->rows.size() == 1029);
    const auto first = TtTime::fromCalendar({.year = 1900, .month = 1, .day = 1});
    REQUIRE(first.has_value());
    REQUIRE(fixture->rows.front().tt == *first);

    constexpr std::int64_t kDeltaTPicoseconds = 69'213'867'187'500; // 420 / 2^19 day
    std::set<std::int64_t> timesOfDay;
    for (std::size_t i = 0; i < fixture->rows.size(); ++i) {
        const RotationAtEpoch& row = fixture->rows.at(i);
        CAPTURE(i, row.tt, row.ut1);
        REQUIRE(isRotation(row.celestialToTerrestrial, kRotationTolerance));
        timesOfDay.insert(row.tt.picosecondOfDay());
        // Every UT1 is its TT less the DeltaT the header names, exactly.
        REQUIRE(ttFromUt1(row.ut1, DeltaT::fromPicoseconds(kDeltaTPicoseconds).value()) == row.tt);
        if (i == 0) continue;
        REQUIRE_THAT(row.tt.modifiedJulianDay() - fixture->rows.at(i - 1).tt.modifiedJulianDay(),
                     WithinAbsOf(71.0, Tolerance{1.0}));
    }
    // Every row at a different time of day, which is what makes the Earth
    // rotation angle sweep rather than repeat.
    REQUIRE(timesOfDay.size() == fixture->rows.size());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the generated Sun fixture reads, in metres", "[fixture][horizons]") {
    const auto fixture = readStateVectors(std::filesystem::path{dataDirectory()} / "horizons" /
                                          "sun-geocentric.txt");
    if (!fixture.has_value() && fixture.error().kind == FixtureErrorKind::FileNotFound) {
        SKIP("data/horizons/sun-geocentric.txt has not been generated on this machine: "
             "see data/horizons/README.md");
    }
    INFO(errorName(fixture) << " at line " << (fixture ? std::size_t{0} : fixture.error().line));
    REQUIRE(fixture.has_value());

    REQUIRE(headerValue(fixture->header, "target") == "Sun (10)");
    REQUIRE(headerValue(fixture->header, "center") == "Earth (399)");
    REQUIRE(headerValue(fixture->header, "ephemeris") == "DE441");
    REQUIRE(fixture->states.size() == 40);

    // Strictly increasing, from J2000.0.
    const auto j2000 = TdbTime::fromCalendar({.year = 2000, .month = 1, .day = 1, .hour = 12});
    REQUIRE(j2000.has_value());
    REQUIRE(fixture->states.front().epoch == *j2000);
    for (std::size_t i = 1; i < fixture->states.size(); ++i) {
        CAPTURE(i);
        REQUIRE(fixture->states.at(i - 1).epoch < fixture->states.at(i).epoch);
    }

    // In metres: every distance between 0.98 and 1.02 AU. Kilometres read as
    // metres would put the Sun 150,000 m away, and the other mistake 1.5e14 m.
    // A plausibility check on the unit, not an accuracy claim -- that is
    // M1-08's, against these same rows.
    constexpr f64 kAstronomicalUnitMetres = 149'597'870'700.0; // IAU 2012 B2, exact
    for (const StateAtEpoch& state : fixture->states) {
        const f64 distance = length(state.position).value() / kAstronomicalUnitMetres;
        CAPTURE(distance);
        REQUIRE(distance > 0.98);
        REQUIRE(distance < 1.02);
    }
}
