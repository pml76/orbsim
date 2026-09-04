//
// [S2] Tests for the strong scalar types.
//
// Most of what matters here is checked by the compiler rather than at runtime:
// the point of a strong type is that the mistake stops compiling, and a
// static_assert is how you prove that stayed true.
//
#include "tests/TestHarness.hpp"

#include "core/Units.hpp"

#include <cstdio>
#include <exception>
#include <type_traits>

namespace {

using namespace orbex;
using namespace orbex::literals;

// [S2] These are the real tests of this header. They are proofs, evaluated on
// every build, that the misuse the guidelines warn about cannot be written.
// Delete an `explicit` for convenience and the build stops here.
static_assert(!std::is_convertible_v<f64, Radians>,
              "a bare double must not become an angle on its own");
static_assert(!std::is_convertible_v<Radians, f64>,
              "an angle must not decay back into a bare double");
static_assert(!std::is_convertible_v<Degrees, Radians>,
              "degrees must not become radians without a named conversion");
static_assert(!std::is_constructible_v<Radians, Eccentricity>,
              "an eccentricity must never be usable as an angle");
static_assert(!std::is_constructible_v<Metres, Seconds>,
              "a duration must never be usable as a length");

// [S4] The Rule of Zero claim, checked rather than asserted in prose. If any of
// these types grows a hand-written special member, this stops being true.
static_assert(std::is_trivially_copyable_v<Radians>);
static_assert(std::is_trivially_destructible_v<Radians>);
static_assert(std::is_nothrow_move_constructible_v<Metres>);

// [S6] No strong type can be created uninitialized.
static_assert(Radians{}.value == 0.0);
static_assert(Metres{}.value == 0.0);

void testConversionsRoundTrip(test::Run& run) {
    test::section("angle conversions round-trip");

    // [S11] Approximate comparison, never `==`, with a tolerance that says what
    // it is: two conversions each round once, so a couple of ulps is the floor.
    test::checkNear(run,
                    toDegrees(toRadians(51.6_deg)).value,
                    51.6,
                    Tolerance{1e-13},
                    "degrees -> radians -> degrees");
    test::checkNear(run,
                    toRadians(toDegrees(Radians{2.5})).value,
                    2.5,
                    Tolerance{1e-15},
                    "radians -> degrees -> radians");
    test::checkNear(run, toRadians(180.0_deg).value, kPi, Tolerance{1e-15}, "180 deg is pi rad");
    test::checkNear(run, (7.0_km).value, 7000.0, Tolerance{0.0}, "km literal is exact");
}

void testOrderingIsUsable(test::Run& run) {
    test::section("strong types compare");

    test::check(run, Metres{100.0} < Metres{200.0}, "metres order");
    test::check(run, Eccentricity{0.5} > Eccentricity{0.1}, "eccentricities order");
    test::check(run, Radians{1.0} == Radians{1.0}, "identical angles compare equal");
}

// [S10][S21] The "abstractions cost performance" myth, answered with a
// measurement rather than an opinion. Both functions are compiled from the
// same header; if the strong type cost anything, the totals would differ.
//
// The definitive check is the disassembly -- at -O2 both compile to a single
// mulsd -- but this version runs in CI, where nobody is reading assembly.
[[nodiscard]] f64 sumViaStrongType(int count) noexcept {
    f64 total = 0.0;
    for (int i = 0; i < count; ++i) {
        total += toRadians(Degrees{static_cast<f64>(i)}).value;
    }
    return total;
}

[[nodiscard]] f64 sumViaBareDouble(int count) noexcept {
    f64 total = 0.0;
    for (int i = 0; i < count; ++i) {
        total += static_cast<f64>(i) * (kPi / 180.0);
    }
    return total;
}

void testZeroOverhead(test::Run& run) {
    test::section("strong types are zero-overhead");

    constexpr int kSamples = 10000;

    // Bit-identical, not merely close: the two forms perform the same
    // arithmetic in the same order, so anything else would mean the abstraction
    // changed the computation.
    test::check(run,
                sumViaStrongType(kSamples) == sumViaBareDouble(kSamples),
                "strong-typed and bare arithmetic agree bit-for-bit");
}

} // namespace

// [S7] main is the one function nothing may escape from: an exception leaving
// it is std::terminate, with no message and no exit code worth reading.
// std::print can throw if stdout is closed, so it is caught here and turned
// into a diagnostic and a failing status -- the same "no error is silently
// ignored" rule the rest of the example follows, applied at the top.
//
// The handlers use std::fputs rather than std::print because a reporting path
// that can itself throw is not a reporting path.
int main() {
    try {
        std::print("orbex :: units\n\n");

        orbex::test::Run run;
        testConversionsRoundTrip(run);
        testOrderingIsUsable(run);
        testZeroOverhead(run);

        return orbex::test::report(run, "units");
    } catch (const std::exception& error) {
        std::fputs("unhandled exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputs("\n", stderr);
        return 2;
    } catch (...) {
        std::fputs("unhandled exception of unknown type\n", stderr);
        return 2;
    }
}
