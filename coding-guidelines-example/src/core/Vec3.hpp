#ifndef ORBEX_CORE_VEC3_HPP
#define ORBEX_CORE_VEC3_HPP
//
// A 3-vector whose components carry their unit.
//
// [S2] `Vec3<units::kMetre>` is a position; `Vec3<units::kMetre /
// units::kSecond>` is a velocity; the two do not add. The parameter is an
// mp-units **reference** rather than one of the named types in Units.hpp, and
// that is the point: `cross(r, v)` is m^2/s, a unit nothing here has a name for
// and nothing should, so the result unit has to be *computed* -- `R1 * R2` --
// rather than looked up in a fixed list.
//
// [S11] f64 throughout. The simulation never sees an f32; the single place
// where narrowing happens is gfx::toCameraRelative, which holds every
// static_cast<f32> in the example.
//
// [S14] Self-contained: this header includes everything it needs and compiles
// on its own. The build proves that mechanically rather than trusting it --
// see the orbex_header_selfcheck target in CMakeLists.txt.
//
// This header sits *above* Units.hpp in the include order as of 2026-09-17.
// It used to sit below it and hold the scalar foundations too; those moved to
// core/Scalar.hpp when the components stopped being bare doubles.
//
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <cmath>
#include <concepts>
#include <utility>

namespace orbex {

// [S4] Rule of Zero: no destructor, no copy, no assignment, no move. The
// compiler writes all of them and cannot get them wrong.
// [S6] Default member initializers apply to every constructor at once,
// including the one somebody adds next year, so a Vec3 cannot be created
// uninitialized. Declaring no constructor at all also keeps this an aggregate,
// which is what lets the designated initialisers below work.
template <auto kReference> struct Vec3 {
    using Component = Scalar<kReference>;

    Component x{};
    Component y{};
    Component z{};

    // [S6] Designated initialisers even here, where the order is obvious: the
    // habit is what keeps `{y, x, z}` from compiling somewhere it is not.
    [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const noexcept {
        return {.x = x + other.x, .y = y + other.y, .z = z + other.z};
    }

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept {
        return {.x = x - other.x, .y = y - other.y, .z = z - other.z};
    }

    [[nodiscard]] constexpr Vec3 operator*(f64 scale) const noexcept {
        return {.x = x * scale, .y = y * scale, .z = z * scale};
    }

    // [S19] Bit identity, which is precisely what a determinism test needs --
    // and which a defaulted `==` is not: it says +0.0 equals -0.0, and a NaN
    // equals nothing. It is deliberately not an approximate comparison either;
    // nearlyEqual is for that, and confusing the two is how a determinism test
    // stops testing. [S11] So Vec3 has no `==` at all -- and two Vec3 of
    // different units are simply different types, so there is nothing to delete
    // here the way there is on Scalar.
    [[nodiscard]] constexpr bool bitIdentical(const Vec3& other) const noexcept {
        return x.bitIdentical(other.x) && y.bitIdentical(other.y) && z.bitIdentical(other.z);
    }
};

// [S2] A direction: what normalising anything gives back. `one` multiplies
// away in the unit algebra, so `cross(Direction, Vec3<R>)` is a `Vec3<R>` and
// the chain stays closed.
using Direction = Vec3<mp_units::one>;

// [S17] The one vector quantity this example needs a name for. A velocity
// would be `Vec3<units::kMetre / units::kSecond>` and is not written down,
// for the same reason `Velocity` is not in Units.hpp.
using Position = Vec3<units::kMetre>;

template <auto R1, auto R2>
[[nodiscard]] constexpr auto dot(const Vec3<R1>& a, const Vec3<R2>& b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

template <auto R1, auto R2>
[[nodiscard]] constexpr auto cross(const Vec3<R1>& a, const Vec3<R2>& b) noexcept {
    return Vec3<R1 * R2>{
        .x = (a.y * b.z) - (a.z * b.y),
        .y = (a.z * b.x) - (a.x * b.z),
        .z = (a.x * b.y) - (a.y * b.x),
    };
}

template <auto R> [[nodiscard]] constexpr auto lengthSquared(const Vec3<R>& v) noexcept {
    return dot(v, v);
}

// [S3] Not constexpr, because std::sqrt only becomes constexpr in C++26.
// Everything above it is, which is the point of the rule: mark what can be, and
// let the caller decide when to evaluate it.
template <auto R> [[nodiscard]] inline Scalar<R> length(const Vec3<R>& v) noexcept {
    return Scalar<R>{std::sqrt(lengthSquared(v).value())};
}

// [S3] A static_assert is a unit test that costs nothing at runtime, runs on
// every build whether or not anyone invokes the test suite, and cannot rot.
inline constexpr Direction kUnitX{.x = Dimensionless{1.0}};
inline constexpr Direction kUnitY{.y = Dimensionless{1.0}};
inline constexpr Direction kUnitZ{.z = Dimensionless{1.0}};
inline constexpr Direction kOneTwoThree{
    .x = Dimensionless{1.0},
    .y = Dimensionless{2.0},
    .z = Dimensionless{3.0},
};
inline constexpr Direction kFourFiveSix{
    .x = Dimensionless{4.0},
    .y = Dimensionless{5.0},
    .z = Dimensionless{6.0},
};
inline constexpr Direction kTwoFourSix{
    .x = Dimensionless{2.0},
    .y = Dimensionless{4.0},
    .z = Dimensionless{6.0},
};
inline constexpr Direction kThreeFourZero{.x = Dimensionless{3.0}, .y = Dimensionless{4.0}};
// Deliberately a second constant with the same components as kOneTwoThree
// rather than a reuse of it: `a - a` is the same expression twice, which
// misc-redundant-expression reports and which would prove nothing about
// subtraction anyway.
inline constexpr Direction kOneTwoThreeAgain{
    .x = Dimensionless{1.0},
    .y = Dimensionless{2.0},
    .z = Dimensionless{3.0},
};

// Every result below is exact, so the tolerance is zero. A vector is compared
// through the squared length of the difference, which is zero only when every
// component is: the components are small integers, so no nonzero difference
// can square to an underflowed zero.
static_assert(nearlyEqual(lengthSquared(cross(kUnitX, kUnitY) - kUnitZ).value(),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(dot(kOneTwoThree, kFourFiveSix).value(), 32.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared(kThreeFourZero).value(), 25.0, Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared(kOneTwoThree - kOneTwoThreeAgain).value(),
                          0.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(lengthSquared((kOneTwoThree * 2.0) - kTwoFourSix).value(),
                          0.0,
                          Tolerance{0.0}));

// [S3] And what the units add: a cross product of two positions is m^2, a unit
// with no name here, and adding a position to a dimensionless vector is a
// compile error rather than a review comment.
// declval rather than `cross(Position{}, Position{})`, and the reason is an
// **upstream clang-tidy bug**, not a style preference: with two or more
// arguments that are each an empty braced-init of an aggregate whose members
// have default member initializers, readability-trailing-comma reads the comma
// *between the arguments* as a trailing comma inside the braces. Three lines
// reproduce it, and they are in the parent's PROJECT_STATE section 8. Confirmed
// on clang-tidy 23.1.0 and 23.1.1, 2026-09-18. declval also says "a Position,
// unevaluated" more plainly, so this stays either way -- but the workaround can
// go once the check is fixed.
using CrossOfTwoPositions = decltype(cross(std::declval<Position>(), std::declval<Position>()));
using SquareMetres = Vec3<units::kMetre * units::kMetre>;
static_assert(std::is_same_v<CrossOfTwoPositions, SquareMetres>,
              "the unit of a cross product is computed, not looked up");
static_assert(std::is_same_v<decltype(length(Position{})), Metres>,
              "and the length of a position is a length");

template <typename A, typename B>
concept vectorAddable = requires(const A& a, const B& b) { a + b; };
static_assert(vectorAddable<Position, Position>);
static_assert(!vectorAddable<Position, Direction>, "a position plus a direction must not compile");

// [S19] bitIdentical compares every component, and tells the two zeros apart.
static_assert(kOneTwoThree.bitIdentical(kOneTwoThreeAgain));
static_assert(!Direction{}.bitIdentical(Direction{.x = Dimensionless{-0.0}}) &&
              !Direction{}.bitIdentical(Direction{.y = Dimensionless{-0.0}}) &&
              !Direction{}.bitIdentical(Direction{.z = Dimensionless{-0.0}}));
static_assert(!std::equality_comparable<Direction> && !std::equality_comparable<Position>,
              "exact equality of a double is spelled out");

} // namespace orbex

#endif // ORBEX_CORE_VEC3_HPP
