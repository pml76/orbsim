#ifndef ORBSIM_VIEW_MAT4_HPP
#define ORBSIM_VIEW_MAT4_HPP
//
// The 4x4 homogeneous transform, carrying both the frames it maps between and
// the units of both spaces (M1-09; ADR 0012 for where it lives, ADR 0020 for
// the units, ADR 0021 for the frames).
//
// **A homogeneous 4x4 has no single unit.** A projective point is a Vec4
// whose xyz carry one reference and whose w carries another; the affine point
// it denotes is xyz / w, so the *ratio* is what is geometrically meaningful
// and the *pair* is what fixes the matrix entries. With the frames, a Mat4
// maps
//
//     Vec4<kFrom, kInXyz, kInW>  ->  Vec4<kTo, kOutXyz, kOutW>
//
// and its four blocks have four different references, every one derived
// rather than declared:
//
//     linear      rows 0-2, columns 0-2    kOutXyz / kInXyz
//     translation rows 0-2, column 3       kOutXyz / kInW
//     bottomRow   row 3,    columns 0-2    kOutW   / kInXyz
//     corner      row 3,    column 3       kOutW   / kInW
//
// That assignment is closed under multiplication, and the frames unify on the
// middle: `projection * view` compiles and `view * projection` does not, a
// world point cannot be handed to a matrix that starts in view space, and
// `inverseRigid` of an A-to-B transform is a B-to-A one, which the compiler
// now knows rather than the reader.
//
// **Column-major**, as GLSL and Vulkan want, so the narrowing at the GPU
// boundary (M1-11) is a copy rather than a transpose.
//
// **The transpose is a dual map**, which is what makes its law general: if M
// maps a to b then transpose(M) maps b* to a*, so the frames swap and
// dualise and every reference inverts. See TransposeOf below.
//
#include "core/Contract.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Frame.hpp"

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

// An affine point or direction, in a frame. `core/Math.hpp`'s Vec3 carries a
// unit but no frame, and it stays that way: framing it would mean framing
// `Position`, which the whole physics uses, and milestone 1's scope fence
// keeps the core frame-free. So the frame is added here, where only the
// renderer sees it.
template <FrameTag kFrame, auto kR> struct FramedVec3 {
    Vec3<kR> v;
};

// Free rather than a member, because a member function would stop
// misc-non-private-member-variables-in-classes ignoring FramedVec3's public
// member -- measured 2026-09-20, the check ignores an all-public class only
// while it declares no member function. The two arguments are
// interchangeable, bit equality being symmetric, so transposing them cannot
// produce a wrong answer: the reason core/Scalar.hpp gives on nearlyEqual.
// The suppression sits on the signature, not above the `template` line:
// NOLINTNEXTLINE covers exactly the next line and the finding is reported
// where the parameters are.
template <FrameTag kFrame, auto kR>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] constexpr bool bitIdentical(const FramedVec3<kFrame, kR>& left,
                                          const FramedVec3<kFrame, kR>& right) noexcept {
    return left.v.bitIdentical(right.v);
}

// A projective point: xyz in one reference, w in another, both in one frame.
template <FrameTag kFrame, auto kXyz, auto kW> struct Vec4 {
    Vec3<kXyz> xyz;
    Scalar<kW> w;
};

// xyz / w: the affine point a projective one denotes, and the only way out of
// homogeneous coordinates.
//
// **The frame does not change here, and that is deliberate.** The divide is a
// change of representation, not of space: a clip point and its normalised
// device coordinates are the same point. What distinguishes them is already
// in the type without a fourth frame -- clip xyz and w are both metres, so
// the quotient is dimensionless, which is exactly what normalised device
// coordinates are. A `Frame::Ndc` was planned and then not added, because
// building it showed the unit already says it.
//
// **w must not be zero, and that is asserted rather than reported** (M1-11,
// register decision 122). A Vec4 whose w is zero is not an affine point at
// all: it is a point at infinity -- a direction -- and the quotient it
// denotes does not exist. Measured before the guard was added: it returned
// **infinity**, and a **not-a-number** when the numerator was zero too, which
// is a point sitting exactly at the camera. That is the "silently coping"
// third option ADR 0002 separates from reporting and asserting.
//
// Asserted and not reported because the caller is the only thing that can be
// wrong: a graphics card never reaches this state -- clipping removes those
// points before the divide -- and a caller projecting points on the processor
// has to clip for the same reason. `absOf(w) > 0.0` rather than `w != 0.0`,
// which is the comparison `-Wfloat-equal` refuses: it rejects both signed
// zeros, and a NaN as well, since every comparison against one is false.
//
// **What it deliberately does not check is the sign**, and the reason is
// worth knowing because the alternative is tempting. Homogeneous coordinates
// are scale-invariant -- (x, y, z, w) and (-x, -y, -z, -w) denote the same
// point -- so a negative w is perfectly valid for a Vec4 in general. It means
// something only under *this project's clip convention*, where w is the
// distance in front of the camera, and there it means the point is behind the
// camera. That is a clip-space concern and belongs to whatever clips, not to
// a generic divide. `isInFrontOfCamera` below is the name for it, and its
// comment carries the measurement that makes it worth having.
template <FrameTag kFrame, auto kXyz, auto kW>
[[nodiscard]] constexpr FramedVec3<kFrame, kXyz / kW>
perspectiveDivide(const Vec4<kFrame, kXyz, kW>& p) noexcept {
    ORBSIM_EXPECTS(absOf(p.w.value()) > 0.0);
    return FramedVec3<kFrame, kXyz / kW>{
        .v =
            Vec3<kXyz / kW>{
                p.xyz.x.value() / p.w.value(),
                p.xyz.y.value() / p.w.value(),
                p.xyz.z.value() / p.w.value(),
            },
    };
}

template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
class Mat4 {
public:
    static constexpr FrameTag kFromFrame = kFrom;
    static constexpr FrameTag kToFrame = kTo;
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
    //
    // Both of these assert their bounds, as the four typed accessors above do.
    // `Row` and `Column` are structural types with no validation of their own
    // -- they exist to stop a transposition, not to stop an overrun -- so a
    // caller is the only thing that can be wrong here, and ADR 0002 says that
    // is asserted. Without the assertion the `.at()` below throws inside a
    // `noexcept` function, which terminates the process with no diagnostic:
    // the one failure mode worse than an assertion, in the one place a Debug
    // build cannot help.
    [[nodiscard]] static constexpr std::size_t index(Row row, Column column) noexcept {
        ORBSIM_EXPECTS(row.value < 4 && column.value < 4);
        return (column.value * 4) + row.value;
    }

    constexpr void set(Row row, Column column, f64 value) noexcept {
        ORBSIM_EXPECTS(row.value < 4 && column.value < 4);
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
template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
[[nodiscard]] constexpr bool
isAffine(const Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>& m) noexcept {
    const std::array<f64, 16> e = m.columnMajor();
    return bitsOf(e.at(3)) == bitsOf(0.0) && bitsOf(e.at(7)) == bitsOf(0.0) &&
           bitsOf(e.at(11)) == bitsOf(0.0) && bitsOf(e.at(15)) == bitsOf(1.0);
}

// Composition: `outer * inner` applies `inner` first, as the mathematics
// reads. **The middle frame and the middle references both unify**, which is
// the whole point: `projection * view` compiles, `view * projection` does
// not, and neither does composing two transforms that do not meet.
template <FrameTag kA,
          FrameTag kB,
          FrameTag kC,
          auto kAXyz,
          auto kAW,
          auto kBXyz,
          auto kBW,
          auto kCXyz,
          auto kCW>
[[nodiscard]] constexpr Mat4<kA, kC, kAXyz, kAW, kCXyz, kCW>
operator*(const Mat4<kB, kC, kBXyz, kBW, kCXyz, kCW>& outer,
          const Mat4<kA, kB, kAXyz, kAW, kBXyz, kBW>& inner) noexcept {
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
    return Mat4<kA, kC, kAXyz, kAW, kCXyz, kCW>::fromColumnMajor(result);
}

// The transpose's type, as the **dual map** it is. If M maps a to b, then
// transpose(M) maps b* to a*: the frames swap and dualise, and every
// reference inverts, because a covector on a space measured in R is measured
// in 1/R.
//
// That formulation is what makes the law general. Written the other way --
// forcing the result back into a matrix between the same two spaces --
// `transpose(A B) == transpose(B) transpose(A)` typechecks only where the
// operands' units happen to line up, which excludes every chain containing a
// projection. As a dual map it composes for all of them: M1 from a to b and
// M2 from b to c give transpose(M1) after transpose(M2), c* to a*, which is
// transpose(M2 M1). Dualising twice is the identity and 1/(1/R) is R, so
// transpose(transpose(M)) is M's own type -- measured on clang 23.1.0,
// gcc-14 and MSVC 19.51 before this was chosen.
//
// Spelled as an alias rather than deduced with `auto`, because a deduced
// return type defeats NRVO and clang's -Wnrvo is an error here.
template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
using TransposeOf = Mat4<dualOf(kTo),
                         dualOf(kFrom),
                         mp_units::one / kOutXyz,
                         mp_units::one / kOutW,
                         mp_units::one / kInXyz,
                         mp_units::one / kInW>;

template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
[[nodiscard]] constexpr TransposeOf<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>
transpose(const Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>& m) noexcept {
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            result.at((column * 4) + row) = e.at((row * 4) + column);
        }
    }
    return TransposeOf<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>::fromColumnMajor(result);
}

// The inverse of a rigid transform: the rotation transposed, and the
// translation turned round by it and negated. Cheaper than a general inverse,
// and exact where one is neither.
//
// **Its type says what it is**: the inverse of an A-to-B transform is a
// B-to-A one. Defined only where the two spaces are parameterised alike, so a
// projection has no rigid inverse and the type refuses it rather than a
// comment asking the reader not to.
template <FrameTag kFrom, FrameTag kTo, auto kXyz, auto kW>
[[nodiscard]] constexpr Mat4<kTo, kFrom, kXyz, kW, kXyz, kW>
inverseRigid(const Mat4<kFrom, kTo, kXyz, kW, kXyz, kW>& m) noexcept {
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
    return Mat4<kTo, kFrom, kXyz, kW, kXyz, kW>::fromColumnMajor(result);
}

// The homogeneous transform: the only way a projection may be applied, and
// the reason transformPoint below can refuse one.
template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
[[nodiscard]] constexpr Vec4<kTo, kOutXyz, kOutW>
transform(const Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>& m,
          const Vec4<kFrom, kInXyz, kInW>& p) noexcept {
    const std::array<f64, 16> e = m.columnMajor();
    const std::array<f64, 4> in{{p.xyz.x.value(), p.xyz.y.value(), p.xyz.z.value(), p.w.value()}};
    std::array<f64, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        f64 sum = 0.0;
        for (std::size_t k = 0; k < 4; ++k) {
            sum += e.at((k * 4) + row) * in.at(k);
        }
        out.at(row) = sum;
    }
    return Vec4<kTo, kOutXyz, kOutW>{
        .xyz = Vec3<kOutXyz>{out.at(0), out.at(1), out.at(2)},
        .w = Scalar<kOutW>{out.at(3)},
    };
}

// A point: the translation applies. Affine only, and the type says so -- kInW
// and kOutW must be one reference, which rules out a projection at compile
// time. The point must already be in the matrix's source frame, which is the
// other half of what the types check here.
template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
    requires(std::is_same_v<Scalar<kInW>, Scalar<kOutW>>)
[[nodiscard]] constexpr FramedVec3<kTo, kOutXyz>
transformPoint(const Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>& m,
               const FramedVec3<kFrom, kInXyz>& p) noexcept {
    ORBSIM_EXPECTS(isAffine(m));
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out.at(row) = (e.at(row) * p.v.x.value()) + (e.at(4 + row) * p.v.y.value()) +
                      (e.at(8 + row) * p.v.z.value()) + e.at(12 + row);
    }
    return FramedVec3<kTo, kOutXyz>{.v = Vec3<kOutXyz>{out.at(0), out.at(1), out.at(2)}};
}

// A direction: the translation does not apply. Two names rather than one
// because a point and a direction transform differently, and nothing else in
// the signature would say which was meant.
template <FrameTag kFrom, FrameTag kTo, auto kInXyz, auto kInW, auto kOutXyz, auto kOutW>
    requires(std::is_same_v<Scalar<kInW>, Scalar<kOutW>>)
[[nodiscard]] constexpr FramedVec3<kTo, kOutXyz>
transformDirection(const Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>& m,
                   const FramedVec3<kFrom, kInXyz>& d) noexcept {
    ORBSIM_EXPECTS(isAffine(m));
    const std::array<f64, 16> e = m.columnMajor();
    std::array<f64, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out.at(row) = (e.at(row) * d.v.x.value()) + (e.at(4 + row) * d.v.y.value()) +
                      (e.at(8 + row) * d.v.z.value());
    }
    return FramedVec3<kTo, kOutXyz>{.v = Vec3<kOutXyz>{out.at(0), out.at(1), out.at(2)}};
}

// **The one place a frame change is declared rather than derived.**
//
// A rotation and a translation are each within a frame -- neither changes
// what space you are in -- so the factories below produce F-to-F transforms.
// A camera's view matrix is their composition, and what makes it
// world-to-view is the camera's definition of view space, not any property of
// the arithmetic. That declaration happens here, once, by name, and it is
// **unchecked**: nothing verifies that the matrix really maps world to view.
// It is an assertion the caller makes, which is why it has a name of its own
// rather than being spelled as an ordinary conversion, and why it does not
// route through `fromColumnMajor` -- that one is for building a matrix from
// data, and the two uses should be greppable apart.
//
// `grep retargetFrame` is the audit. M1-11's camera is expected to be the
// only caller.
template <FrameTag kNewTo,
          FrameTag kFrom,
          FrameTag kTo,
          auto kInXyz,
          auto kInW,
          auto kOutXyz,
          auto kOutW>
[[nodiscard]] constexpr Mat4<kFrom, kNewTo, kInXyz, kInW, kOutXyz, kOutW>
retargetFrame(const Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>& m) noexcept {
    return Mat4<kFrom, kNewTo, kInXyz, kInW, kOutXyz, kOutW>::fromColumnMajor(m.columnMajor());
}

template <FrameTag kFrame, auto kXyz, auto kW>
[[nodiscard]] constexpr Mat4<kFrame, kFrame, kXyz, kW, kXyz, kW> identityMatrix() noexcept {
    Mat4<kFrame, kFrame, kXyz, kW, kXyz, kW> m{};
    for (std::size_t d = 0; d < 4; ++d) {
        m.set(Row{d}, Column{d}, 1.0);
    }
    return m;
}

// The names the renderer uses. A Transform moves a point about; a Projection
// takes a view point to clip space, where w is metres and the divide
// therefore lands in dimensionless coordinates.
template <FrameTag kFrom, FrameTag kTo>
using Transform = Mat4<kFrom, kTo, units::kMetre, mp_units::one, units::kMetre, mp_units::one>;
using Projection = Mat4<kView, kClip, units::kMetre, mp_units::one, units::kMetre, units::kMetre>;

using WorldTransform = Transform<kWorld, kWorld>;
using WorldToView = Transform<kWorld, kView>;

template <FrameTag kFrame> using FramedPosition = FramedVec3<kFrame, units::kMetre>;
using WorldPosition = FramedPosition<kWorld>;
using ViewPosition = FramedPosition<kView>;

template <FrameTag kFrame> using HomogeneousPoint = Vec4<kFrame, units::kMetre, mp_units::one>;
using WorldPoint = HomogeneousPoint<kWorld>;
using ViewPoint = HomogeneousPoint<kView>;
using ClipPoint = Vec4<kClip, units::kMetre, units::kMetre>;

// Is this clip point in front of the camera?
//
// **A name for a hazard that measurement showed is worse than the one it sits
// beside** (M1-11, register decision 122). `perspectiveDivide` above refuses a
// zero w, which is loud: infinity, or a not-a-number. A *negative* w is the
// quiet one. Under this project's convention w is the distance in front of the
// camera, so a negative w is a point behind it -- and the divide by a negative
// number flips both signs, so the point lands back **on screen, mirrored**.
//
// Measured, and this is the number that earned this function its place: a
// point 10 m behind the camera at view-space (-0.5, -0.4) produces x and y
// **bit-identical** to a point 10 m in front at (+0.5, +0.4). Not close --
// identical. The only thing that tells them apart is the depth, which comes
// out negative and therefore outside [0, 1]; a caller who does not think to
// check it sees a plausible position and no warning at all. That is the
// "finite, plausible, wrong" shape ADR 0022 exists to remove, and here it is
// in the render maths.
//
// It is **not** folded into `perspectiveDivide`, because a negative w is
// legitimate for a Vec4 in general: homogeneous coordinates are
// scale-invariant. It is only wrong in clip space, so the check lives on the
// clip-space type and nowhere else.
//
// Nothing clips on the processor yet, so nothing calls this in `src/`. It
// exists so that the first thing that does has a name to reach for rather than
// a sign test somebody has to think of, and so that the measurement above is
// attached to code rather than to a comment in a status file.
[[nodiscard]] constexpr bool isInFrontOfCamera(const ClipPoint& p) noexcept {
    return p.w.value() > 0.0;
}

[[nodiscard]] constexpr WorldTransform identityTransform() noexcept {
    return identityMatrix<kWorld, units::kMetre, mp_units::one>();
}

template <FrameTag kFrame>
[[nodiscard]] constexpr Transform<kFrame, kFrame> translationOf(const Position& t) noexcept {
    Transform<kFrame, kFrame> m = identityMatrix<kFrame, units::kMetre, mp_units::one>();
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
template <FrameTag kFrame>
[[nodiscard]] inline Transform<kFrame, kFrame> rotationOf(const Quat& q) noexcept {
    const std::array<Direction, 3> turned{
        {
            q.rotate(Direction{1.0, 0.0, 0.0}),
            q.rotate(Direction{0.0, 1.0, 0.0}),
            q.rotate(Direction{0.0, 0.0, 1.0}),
        },
    };
    Transform<kFrame, kFrame> m = identityMatrix<kFrame, units::kMetre, mp_units::one>();
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

inline constexpr WorldTransform kShiftForAssertions =
    translationOf<kWorld>(Position{1.0, 2.0, 3.0});
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

// The transpose, as a dual map.
static_assert(transpose(transpose(kShiftForAssertions)).bitIdentical(kShiftForAssertions),
              "transposing twice returns the original, exactly");
static_assert(std::is_same_v<decltype(transpose(transpose(kShiftForAssertions))), WorldTransform>,
              "and with the original's type: dualising twice is the identity, and 1/(1/R) is R");
static_assert(std::is_same_v<decltype(transpose(std::declval<Projection>())),
                             Mat4<dualOf(kClip),
                                  dualOf(kView),
                                  mp_units::one / units::kMetre,
                                  mp_units::one / units::kMetre,
                                  mp_units::one / units::kMetre,
                                  mp_units::one>>,
              "a projection's transpose runs from the dual of clip to the dual of view");
static_assert(
    std::is_same_v<decltype(transpose(transpose(std::declval<Projection>()))), Projection>,
    "and it round-trips too, which the same-direction formulation could not");

inline constexpr WorldPosition kPointForAssertions{.v = Position{10.0, 20.0, 30.0}};
static_assert(nearlyEqual(transformPoint(kShiftForAssertions, kPointForAssertions).v.x.value(),
                          11.0,
                          Tolerance{0.0}),
              "a translation moves a point");
static_assert(nearlyEqual(transformDirection(kShiftForAssertions, kPointForAssertions).v.x.value(),
                          10.0,
                          Tolerance{0.0}),
              "and leaves a direction alone");

// What the units buy, asserted rather than described.
static_assert(std::is_same_v<decltype(kShiftForAssertions.translation(Row{0})), Metres>,
              "an affine transform's translation column is metres");
static_assert(std::is_same_v<decltype(std::declval<Projection>().translation(Row{2})), Metres>,
              "and so is a projection's near-plane entry");
static_assert(std::is_same_v<decltype(std::declval<Projection>().corner()),
                             Scalar<units::kMetre / mp_units::one>>,
              "a projection's corner is metres, which is why one Mat4 cannot span the chain");
static_assert(std::is_same_v<decltype(perspectiveDivide(std::declval<ClipPoint>())),
                             FramedVec3<kClip, units::kMetre / units::kMetre>>,
              "clip over w is dimensionless, which is what normalised device coordinates are");

// What the frames buy.
static_assert(
    std::is_same_v<decltype(std::declval<Projection>() * std::declval<WorldToView>()),
                   Mat4<kWorld, kClip, units::kMetre, mp_units::one, units::kMetre, units::kMetre>>,
    "projection after world-to-view is a world-to-clip projection");
static_assert(
    std::is_same_v<decltype(inverseRigid(std::declval<WorldToView>())), Transform<kView, kWorld>>,
    "the rigid inverse of a world-to-view transform is a view-to-world one");
static_assert(
    std::is_same_v<decltype(retargetFrame<kView>(std::declval<WorldTransform>())), WorldToView>,
    "and retargetFrame is how a world-to-world transform becomes world-to-view");

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

static_assert(composable<Projection, WorldToView>, "projection * view is the pipeline");
static_assert(!composable<WorldToView, Projection>,
              "view * projection is the classic bug and must not compile");
static_assert(composable<WorldTransform, WorldTransform>,
              "two transforms within one frame compose either way");
static_assert(composable<WorldToView, WorldTransform>,
              "and a world-to-view after a world-to-world does compose");
static_assert(!composable<WorldTransform, WorldToView>,
              "while a world-to-world after a world-to-view does not: the frames do not meet");

static_assert(pointTransformable<WorldToView, WorldPosition>,
              "a world-to-view transform moves a world point");
static_assert(!pointTransformable<WorldToView, ViewPosition>,
              "and refuses a point that is already in view space");
static_assert(!pointTransformable<Projection, ViewPosition>,
              "a projection goes through transform() and the divide, never transformPoint()");

static_assert(homogeneousTransformable<Projection, ViewPoint>);
static_assert(!homogeneousTransformable<Projection, ClipPoint>,
              "a clip point must not be projected a second time");
static_assert(!homogeneousTransformable<Projection, WorldPoint>,
              "nor may a world point skip the view transform");

static_assert(rigidInvertible<WorldToView>);
static_assert(!rigidInvertible<Projection>, "a projection has no rigid inverse");

// One level up from ADR 0019: a transform of velocities is not a transform of
// positions, and neither will touch the other's vectors.
using VelocityTransform = Mat4<kWorld,
                               kWorld,
                               units::kMetre / units::kSecond,
                               mp_units::one,
                               units::kMetre / units::kSecond,
                               mp_units::one>;
static_assert(!composable<VelocityTransform, WorldTransform>,
              "a transform of velocities does not compose with one of positions");
static_assert(
    !pointTransformable<WorldTransform, FramedVec3<kWorld, units::kMetre / units::kSecond>>,
    "nor does one move a velocity");

} // namespace orb::view

#endif // ORBSIM_VIEW_MAT4_HPP
