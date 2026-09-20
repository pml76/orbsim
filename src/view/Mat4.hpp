#ifndef ORBSIM_VIEW_MAT4_HPP
#define ORBSIM_VIEW_MAT4_HPP
//
// The 4x4 homogeneous transform, with its units in the type system (M1-09,
// ADR 0012 for where it lives and ADR 0020 for the parameterisation).
//
// **A homogeneous 4x4 has no single unit**, which is why this type takes four
// template parameters rather than none. A projective point is a Vec4 whose xyz
// carry one reference and whose w carries another; the affine point it denotes
// is xyz / w, so the *ratio* is what is geometrically meaningful and the pair
// is what fixes the matrix entries. A Mat4 therefore maps
//
//     Vec4<kInXyz, kInW>  ->  Vec4<kOutXyz, kOutW>
//
// and its four blocks have four different references, every one of them
// derived from those four rather than declared:
//
//     linear      rows 0-2, columns 0-2    kOutXyz / kInXyz
//     translation rows 0-2, column 3       kOutXyz / kInW
//     bottomRow   row 3,    columns 0-2    kOutW   / kInXyz
//     corner      row 3,    column 3       kOutW   / kInW
//
// That assignment is closed under multiplication -- each block of a product
// comes out in its own reference from both of its terms -- which is what lets
// `projection * view` compile and `view * projection` not.
//
// **Column-major**, as GLSL and Vulkan want, so that narrowing at the GPU
// boundary (M1-11) is a copy rather than a transpose.
//
// What this does NOT distinguish is *frames*: world and view are both metres,
// so a model matrix and a view matrix are one type here. ADR 0019 left frames
// open deliberately and milestone 1's scope fence keeps them out; the
// signature admits them later. ADR 0020 says so.
//
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

#include <mp-units/framework.h>

namespace orb::view {

// A pure number: the homogeneous coordinate of an ordinary point, among other
// things. Not `Eccentricity`, which is dimensionless but is its own *kind* and
// deliberately converts to nothing else -- which the M1-09 spike found by
// trying it. Local to this header rather than a tenth name in core/Units.hpp,
// because the need is render-side and CODING_GUIDELINES section 12 pushes the
// dependency the other way.
using Dimensionless = Scalar<mp_units::one>;

// Strong index types. A transposed element access is the defect this file
// exists to avoid, and clang-tidy cannot help: measured 2026-09-20,
// bugprone-easily-swappable-parameters silences any pair used together in one
// expression, and `elements_.at((column * 4) + row)` is one expression.
struct Row {
    std::size_t value{};
};
struct Column {
    std::size_t value{};
};

// A projective point: xyz in one reference, w in another.
template <auto kXyz, auto kW> struct Vec4 {
    Vec3<kXyz> xyz;
    Scalar<kW> w;
};

// xyz / w: the affine point a projective one denotes, and the only way out of
// homogeneous coordinates. Its reference is the ratio, so clip space -- metres
// over metres -- divides to the dimensionless normalised device coordinates
// the GPU wants.
template <auto kXyz, auto kW>
[[nodiscard]] constexpr Vec3<kXyz / kW> perspectiveDivide(const Vec4<kXyz, kW>& p) noexcept {
    return Vec3<kXyz / kW>{
        p.xyz.x.value() / p.w.value(),
        p.xyz.y.value() / p.w.value(),
        p.xyz.z.value() / p.w.value(),
    };
}

template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW> class Mat4 {
public:
    static constexpr auto kLinearRef = kOutXyz / kInXyz;
    static constexpr auto kTranslationRef = kOutXyz / kInW;
    static constexpr auto kBottomRowRef = kOutW / kInXyz;
    static constexpr auto kCornerRef = kOutW / kInW;

    constexpr Mat4() noexcept = default;

    // From sixteen numbers in the order the GPU reads them: column 0 first.
    // Named, because a bare array of sixteen doubles is exactly the thing this
    // type exists to stop being passed around untyped.
    [[nodiscard]] static constexpr Mat4 fromColumnMajor(const std::array<f64, 16>& e) noexcept {
        Mat4 m{};
        m.elements_ = e;
        return m;
    }

    // The storage, for the one place that needs it: the f64 -> f32 narrowing
    // at the GPU boundary, which is M1-11's and lives in one named function.
    //
    // **By value, not by reference.** A reference draws clang's
    // -Wlifetime-safety-intra-tu-suggestions, which wants
    // [[clang::lifetimebound]] -- and the macro for that lives in
    // render/VulkanHandle.hpp, whose own comment says it moves down a layer
    // the moment a second one wants it. Copying 128 bytes once per matrix at
    // the upload boundary is cheaper than moving a macro across two libraries
    // in a task about neither, so the reference is what gives way. Reconsider
    // if a caller ever reads this per vertex rather than per frame.
    [[nodiscard]] constexpr std::array<f64, 16> columnMajor() const noexcept { return elements_; }

    [[nodiscard]] constexpr Scalar<kLinearRef> linear(Row row, Column column) const noexcept {
        ORBSIM_EXPECTS(row.value < 3 && column.value < 3);
        return Scalar<kLinearRef>{elements_.at(index(row, column))};
    }
    [[nodiscard]] constexpr Scalar<kTranslationRef> translation(Row row) const noexcept {
        ORBSIM_EXPECTS(row.value < 3);
        return Scalar<kTranslationRef>{elements_.at(index(row, Column{3}))};
    }
    [[nodiscard]] constexpr Scalar<kBottomRowRef> bottomRow(Column column) const noexcept {
        ORBSIM_EXPECTS(column.value < 3);
        return Scalar<kBottomRowRef>{elements_.at(index(Row{3}, column))};
    }
    [[nodiscard]] constexpr Scalar<kCornerRef> corner() const noexcept {
        return Scalar<kCornerRef>{elements_.at(index(Row{3}, Column{3}))};
    }

    // Column-major: the element at (row, column) is at column * 4 + row.
    [[nodiscard]] static constexpr std::size_t index(Row row, Column column) noexcept {
        return (column.value * 4) + row.value;
    }

    constexpr void set(Row row, Column column, f64 value) noexcept {
        elements_.at(index(row, column)) = value;
    }

    // No `==`: on doubles it is the comparison CODING_GUIDELINES section 11
    // forbids (ADR 0017). Bit identity says so by name, as it does on Vec3
    // and Quat, and it is the right claim for the two exact laws this type
    // has -- the identity, and transpose(AB) = transpose(B)transpose(A).
    [[nodiscard]] constexpr bool bitIdentical(const Mat4& other) const noexcept {
        for (std::size_t i = 0; i < 16; ++i) {
            if (bitsOf(elements_.at(i)) != bitsOf(other.elements_.at(i))) return false;
        }
        return true;
    }

private:
    std::array<f64, 16> elements_{};
};

// Is the bottom row exactly (0, 0, 0, 1)? Public because it is
// transformPoint's precondition, and a precondition nothing can check from
// outside is one nobody can test -- the argument core/Math.hpp makes for
// isRotation. Exact rather than approximate, and bit-wise rather than `==`:
// an affine matrix's bottom row is set, never computed, so anything else is a
// different kind of matrix and not a rounding of this one.
template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
[[nodiscard]] constexpr bool isAffine(const Mat4<kInXyz, kInW, kOutXyz, kOutW>& m) noexcept {
    const std::array<f64, 16> e = m.columnMajor();
    return bitsOf(e.at(3)) == bitsOf(0.0) && bitsOf(e.at(7)) == bitsOf(0.0) &&
           bitsOf(e.at(11)) == bitsOf(0.0) && bitsOf(e.at(15)) == bitsOf(1.0);
}

// Composition: `outer * inner` applies `inner` first, as the mathematics
// reads. The middle space has to match, and that is the whole point of the
// parameterisation -- it is what makes `projection * view` compile and
// `view * projection` not, because a projection's output w is metres where a
// transform's input w is dimensionless.
template <auto kAXyz, auto kAW, auto kBXyz, auto kBW, auto kCXyz, auto kCW>
[[nodiscard]] constexpr Mat4<kAXyz, kAW, kCXyz, kCW>
operator*(const Mat4<kBXyz, kBW, kCXyz, kCW>& outer,
          const Mat4<kAXyz, kAW, kBXyz, kBW>& inner) noexcept {
    const std::array<f64, 16> a = outer.columnMajor();
    const std::array<f64, 16> b = inner.columnMajor();
    std::array<f64, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            f64 sum = 0.0;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a.at((k * 4) + row) * b.at((column * 4) + k);
            }
            result.at((column * 4) + row) = sum;
        }
    }
    return Mat4<kAXyz, kAW, kCXyz, kCW>::fromColumnMajor(result);
}

// The transpose's type, derived rather than declared. T[i][j] = M[j][i], and
// solving the four block rules for the result gives
//
//     transpose(Mat4<a, b, c, d>) : Mat4<a, c * a / d, c, c * a / b>
//
// which round-trips -- transpose(transpose(M)) is M's own type -- and, for
// the affine matrices the row/column-major test uses, composes as the
// transpose law requires. Spelled as an alias rather than deduced with
// `auto`, because a deduced return type defeats NRVO and clang's -Wnrvo is an
// error here (measured 2026-09-20).
template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
using TransposeOf = Mat4<kInXyz, kOutXyz * kInXyz / kOutW, kOutXyz, kOutXyz * kInXyz / kInW>;

template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
[[nodiscard]] constexpr TransposeOf<kInXyz, kInW, kOutXyz, kOutW>
transpose(const Mat4<kInXyz, kInW, kOutXyz, kOutW>& m) noexcept {
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            result.at((column * 4) + row) = e.at((row * 4) + column);
        }
    }
    return TransposeOf<kInXyz, kInW, kOutXyz, kOutW>::fromColumnMajor(result);
}

// The inverse of a rigid transform: the rotation transposed, and the
// translation turned round by it and negated. Cheaper than a general inverse,
// and exact where one is neither.
//
// Only defined where the two spaces are parameterised alike, so a projection
// has no rigid inverse and the type says so rather than a comment.
template <auto kXyz, auto kW>
[[nodiscard]] constexpr Mat4<kXyz, kW, kXyz, kW>
inverseRigid(const Mat4<kXyz, kW, kXyz, kW>& m) noexcept {
    ORBSIM_EXPECTS(isAffine(m));
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 16> result{};
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            result.at((column * 4) + row) = e.at((row * 4) + column);
        }
    }
    // -R^T t, component by component: the row-th component is
    // -sum_k R(k, row) * t_k, and R(k, row) is column-major element
    // row * 4 + k. Writing e.at(row), e.at(4 + row), e.at(8 + row) here
    // instead computes -R t, which is a different matrix and was the first
    // thing this file got wrong -- caught by the round-trip test, which came
    // out 1.9 m off at a scale of 1 m.
    for (std::size_t row = 0; row < 3; ++row) {
        result.at(12 + row) =
            -((e.at((row * 4) + 0) * e.at(12)) + (e.at((row * 4) + 1) * e.at(13)) +
              (e.at((row * 4) + 2) * e.at(14)));
    }
    result.at(15) = 1.0;
    return Mat4<kXyz, kW, kXyz, kW>::fromColumnMajor(result);
}

// The homogeneous transform: the only way a projection may be applied, and
// the reason transformPoint below can refuse one.
template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
[[nodiscard]] constexpr Vec4<kOutXyz, kOutW> transform(const Mat4<kInXyz, kInW, kOutXyz, kOutW>& m,
                                                       const Vec4<kInXyz, kInW>& p) noexcept {
    const std::array<f64, 16> e = m.columnMajor();
    // Doubly braced: gcc's -Wmissing-braces wants the inner one for a
    // std::array, where clang and MSVC accept the single form.
    const std::array<f64, 4> in{{p.xyz.x.value(), p.xyz.y.value(), p.xyz.z.value(), p.w.value()}};
    std::array<f64, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        f64 sum = 0.0;
        for (std::size_t k = 0; k < 4; ++k) {
            sum += e.at((k * 4) + row) * in.at(k);
        }
        out.at(row) = sum;
    }
    return Vec4<kOutXyz, kOutW>{
        Vec3<kOutXyz>{out.at(0), out.at(1), out.at(2)},
        Scalar<kOutW>{out.at(3)},
    };
}

// A point: the translation applies. Affine only, and the type says so -- kInW
// and kOutW must be one reference, which rules out a projection at compile
// time. That the bottom row is (0, 0, 0, 1) in *value* is the precondition
// below, because only a bug can break it.
template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
    requires(std::is_same_v<Scalar<kInW>, Scalar<kOutW>>)
[[nodiscard]] constexpr Vec3<kOutXyz> transformPoint(const Mat4<kInXyz, kInW, kOutXyz, kOutW>& m,
                                                     const Vec3<kInXyz>& p) noexcept {
    ORBSIM_EXPECTS(isAffine(m));
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out.at(row) = (e.at(row) * p.x.value()) + (e.at(4 + row) * p.y.value()) +
                      (e.at(8 + row) * p.z.value()) + e.at(12 + row);
    }
    return Vec3<kOutXyz>{out.at(0), out.at(1), out.at(2)};
}

// A direction: the translation does not apply. Two names rather than one
// because a point and a direction transform differently, and nothing else in
// the signature would say which was meant.
template <auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
    requires(std::is_same_v<Scalar<kInW>, Scalar<kOutW>>)
[[nodiscard]] constexpr Vec3<kOutXyz>
transformDirection(const Mat4<kInXyz, kInW, kOutXyz, kOutW>& m, const Vec3<kInXyz>& d) noexcept {
    ORBSIM_EXPECTS(isAffine(m));
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out.at(row) = (e.at(row) * d.x.value()) + (e.at(4 + row) * d.y.value()) +
                      (e.at(8 + row) * d.z.value());
    }
    return Vec3<kOutXyz>{out.at(0), out.at(1), out.at(2)};
}

template <auto kXyz, auto kW>
[[nodiscard]] constexpr Mat4<kXyz, kW, kXyz, kW> identityMatrix() noexcept {
    Mat4<kXyz, kW, kXyz, kW> m{};
    for (std::size_t d = 0; d < 4; ++d) {
        m.set(Row{d}, Column{d}, 1.0);
    }
    return m;
}

// The names the renderer uses. A Transform moves a point about within one
// parameterisation; a Projection takes a world point to clip space, where w
// is metres and the divide therefore lands in dimensionless coordinates.
using Transform = Mat4<units::kMetre, mp_units::one, units::kMetre, mp_units::one>;
using Projection = Mat4<units::kMetre, mp_units::one, units::kMetre, units::kMetre>;
using WorldPoint = Vec4<units::kMetre, mp_units::one>;
using ClipPoint = Vec4<units::kMetre, units::kMetre>;

[[nodiscard]] constexpr Transform identityTransform() noexcept {
    return identityMatrix<units::kMetre, mp_units::one>();
}

[[nodiscard]] constexpr Transform translationOf(const Position& t) noexcept {
    Transform m = identityTransform();
    m.set(Row{0}, Column{3}, t.x.value());
    m.set(Row{1}, Column{3}, t.y.value());
    m.set(Row{2}, Column{3}, t.z.value());
    return m;
}

// The rotation a unit quaternion represents. Built by turning the three basis
// directions, so this is core/Math.hpp's rotate() rather than a second
// transcription of the quaternion-to-matrix identities: one of the two is
// enough, and the one with a suite of its own is the one to reuse
// (VERIFICATION.md rule 2 -- a second copy would agree with the first for
// reasons that have nothing to do with either being right).
[[nodiscard]] inline Transform rotationOf(const Quat& q) noexcept {
    const std::array<Direction, 3> turned{
        {
            q.rotate(Direction{1.0, 0.0, 0.0}),
            q.rotate(Direction{0.0, 1.0, 0.0}),
            q.rotate(Direction{0.0, 0.0, 1.0}),
        },
    };
    Transform m = identityTransform();
    for (std::size_t column = 0; column < 3; ++column) {
        m.set(Row{0}, Column{column}, turned.at(column).x.value());
        m.set(Row{1}, Column{column}, turned.at(column).y.value());
        m.set(Row{2}, Column{column}, turned.at(column).z.value());
    }
    return m;
}

// Compile-time tests. A static_assert is a unit test that costs nothing at
// runtime, runs on every build whether or not the suite is invoked, and
// cannot rot (CODING_GUIDELINES section 3).

// Multiplication and the identity are usable in a constant expression, which
// is what the task asks to be proved.
inline constexpr Transform kShiftForAssertions = translationOf(Position{1.0, 2.0, 3.0});
static_assert(isAffine(kShiftForAssertions));
static_assert(isAffine(identityTransform()));
static_assert((kShiftForAssertions * identityTransform()).bitIdentical(kShiftForAssertions),
              "the identity is exactly the multiplicative identity, at compile time");
static_assert((identityTransform() * kShiftForAssertions).bitIdentical(kShiftForAssertions),
              "on both sides");
static_assert(nearlyEqual((kShiftForAssertions * kShiftForAssertions).translation(Row{0}).value(),
                          2.0,
                          Tolerance{0.0}),
              "two shifts compose by adding");
static_assert(transpose(transpose(kShiftForAssertions)).bitIdentical(kShiftForAssertions),
              "transposing twice returns the original, exactly");
static_assert(nearlyEqual(transformPoint(kShiftForAssertions, Position{10.0, 20.0, 30.0}).x.value(),
                          11.0,
                          Tolerance{0.0}),
              "a translation moves a point");
static_assert(
    nearlyEqual(transformDirection(kShiftForAssertions, Position{10.0, 20.0, 30.0}).x.value(),
                10.0,
                Tolerance{0.0}),
    "and leaves a direction alone");

// What the units buy, asserted rather than described. The blocks carry the
// four references the parameterisation claims.
static_assert(std::is_same_v<decltype(kShiftForAssertions.translation(Row{0})), Metres>,
              "an affine transform's translation column is metres");
static_assert(std::is_same_v<decltype(std::declval<Projection>().translation(Row{2})), Metres>,
              "and so is a projection's near-plane entry");
static_assert(std::is_same_v<decltype(std::declval<Projection>().corner()),
                             Scalar<units::kMetre / mp_units::one>>,
              "a projection's corner is metres, which is why one Mat4 cannot span the chain");
static_assert(
    std::is_same_v<decltype(std::declval<Projection>() * std::declval<Transform>()), Projection>,
    "projection * view is a world-to-clip projection");
static_assert(std::is_same_v<decltype(perspectiveDivide(std::declval<ClipPoint>())),
                             Vec3<units::kMetre / units::kMetre>>,
              "and clip over w is dimensionless, which is what normalised device coordinates are");

// The errors, refused. Concepts rather than bare requires-expressions,
// because a requires-expression on non-dependent operands is diagnosed rather
// than evaluated -- the note core/Units.hpp carries.
template <typename A, typename B>
concept composable = requires(const A& a, const B& b) { a * b; };
template <typename M, typename V>
concept pointTransformable = requires(const M& m, const V& v) { transformPoint(m, v); };
template <typename M, typename V>
concept homogeneousTransformable = requires(const M& m, const V& v) { transform(m, v); };
template <typename M>
concept rigidInvertible = requires(const M& m) { inverseRigid(m); };

static_assert(composable<Projection, Transform>, "projection * view is the pipeline");
static_assert(!composable<Transform, Projection>,
              "view * projection is the classic bug and must not compile");
static_assert(composable<Transform, Transform>, "two affine transforms compose either way");
static_assert(pointTransformable<Transform, Position>, "a transform moves a point");
static_assert(!pointTransformable<Projection, Position>,
              "a projection goes through transform() and the divide, never transformPoint()");
static_assert(homogeneousTransformable<Projection, WorldPoint>);
static_assert(!homogeneousTransformable<Projection, ClipPoint>,
              "a clip point must not be projected a second time");
static_assert(rigidInvertible<Transform>);
static_assert(!rigidInvertible<Projection>, "a projection has no rigid inverse");

// One level up from ADR 0019: a transform of velocities is not a transform of
// positions, and neither will touch the other's vectors.
using VelocityTransform = Mat4<units::kMetre / units::kSecond,
                               mp_units::one,
                               units::kMetre / units::kSecond,
                               mp_units::one>;
static_assert(!composable<VelocityTransform, Transform>,
              "a transform of velocities does not compose with one of positions");
static_assert(!pointTransformable<Transform, Velocity>, "nor does one move a velocity");

} // namespace orb::view

#endif // ORBSIM_VIEW_MAT4_HPP
