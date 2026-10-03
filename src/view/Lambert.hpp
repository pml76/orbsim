#ifndef ORBSIM_VIEW_LAMBERT_HPP
#define ORBSIM_VIEW_LAMBERT_HPP
//
// A Lambertian surface lit by the Sun, as the lambert probes draw it (M1-18;
// ADR 0014; register decisions 256-271).
//
// **The radiance the shader writes** is
//
//     L = albedo * E * max(cos(theta), 0) / pi        in W/(m^2 sr)
//
// for an irradiance E in W/m^2 at normal incidence and theta the angle
// between the surface's normal and the direction toward the Sun. A perfectly
// diffuse surface sends the light it does not absorb into the half-space
// above it with the same radiance in every direction, and pi steradians is
// that half-space's projected solid angle -- the definition of a Lambertian
// reflector, and where the pi comes from. shaders/lambert.frag evaluates it; nothing on the CPU
// does, because tests/test_radiometry.cpp computes the expected value from the definition itself
// and never from here (VERIFICATION.md rule 2).
//
// **What is here**: the albedo, validated; the probes' patch, a square placed
// and tilted in world space and checked without a GPU by tests/test_lambert.cpp;
// and the shader's block, with the one function that narrows the light to 32
// bits.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/PushConstants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace orb::view {

// --- the albedo ---------------------------------------------------------------

// The ways an albedo can be asked for and not exist. Reported rather than
// asserted, because a scenario or a texture will one day supply it (ADR 0002).
enum class AlbedoError : std::uint8_t {
    NotFinite,        // a NaN or an infinity
    OutsideZeroToOne, // below 0 or above 1
};

[[nodiscard]] constexpr std::string_view describe(AlbedoError error) noexcept {
    switch (error) {
    case AlbedoError::NotFinite:
        return "the albedo must be finite";
    case AlbedoError::OutsideZeroToOne:
        return "the albedo must be from 0 to 1";
    }
    return "unknown albedo error";
}

// The fraction of the light falling on a surface that it scatters back:
// **from 0 to 1, both ends included**, because a surface cannot return more
// energy than it receives and cannot return less than none. A validated type
// (ADR 0022, register decision 258): an albedo of 1.3 is not a brighter
// surface but a broken energy budget, so it cannot be built.
class Albedo {
public:
    [[nodiscard]] static constexpr std::expected<Albedo, AlbedoError> from(f64 value) noexcept {
        if (!isFinite(value)) return std::unexpected(AlbedoError::NotFinite);
        if (value < 0.0 || value > 1.0) return std::unexpected(AlbedoError::OutsideZeroToOne);
        return Albedo{value};
    }
    [[nodiscard]] constexpr f64 value() const noexcept { return value_; }

private:
    explicit constexpr Albedo(f64 value) noexcept : value_(value) {}
    // Initialised although the constructor always sets it: without it,
    // clang-tidy's cppcoreguidelines-pro-type-member-init reports an aggregate
    // holding an Albedo after a member that has a default -- an aggregate that
    // cannot be default-constructed at all, measured on 2026-10-03 (M1-18).
    f64 value_{};
};

// The finite cases at compile time; a NaN is held at run time only, because
// MSVC's constant evaluator disagrees with its own run time about one
// (register decision 205's measurement).
static_assert(Albedo::from(0.0).has_value() && Albedo::from(1.0).has_value() &&
                  Albedo::from(0.3).has_value(),
              "an albedo from 0 to 1 exists, both ends included");
static_assert(!Albedo::from(-0.1).has_value() &&
                  Albedo::from(-0.1).error() == AlbedoError::OutsideZeroToOne &&
                  !Albedo::from(1.1).has_value(),
              "and one outside them does not, by name");
static_assert(describe(AlbedoError::NotFinite) != describe(AlbedoError::OutsideZeroToOne));

// --- the patch ----------------------------------------------------------------

// Where a lambert probe's square is (register decisions 269 and 270). Three
// different types, so nothing can be given in the wrong order.
struct PatchPlacement {
    Position centre; // world metres
    Metres side;     // the square's edge
    // About the horizontal axis, **top away from a camera looking down -z**:
    // zero faces +z, toward that camera; a positive tilt turns the normal
    // upward, to (0, sin, cos).
    Radians tilt;
};

// The square as two triangles, six world positions, each triangle
// counter-clockwise seen from the side its normal points to -- the winding a
// back-face cull keeps (decision 271) -- and the normal itself.
struct SquarePatch {
    std::array<Position, 6> vertices;
    Direction normal;
};

// The square at `placement`. Its edges run along world x and along the
// tilted "up"; a placement that is not finite, or a side that is not
// positive, is a defect in the probe's constants and asserted.
[[nodiscard]] SquarePatch squarePatch(const PatchPlacement& placement) noexcept;

// --- a lambert probe's scene -------------------------------------------------

// Everything a lambert probe fixes about its light and its surface (register
// decisions 256 and 270), which its sidecar records (decision 264). The
// irradiance is held beside the distance it came from rather than recomputed
// here: it is astro/Sun.hpp's solarIrradianceAt of that distance, worked out
// once where the probe is pinned (src/render/Probes.cpp), and orbsim_view
// does not reach into src/astro.
struct LambertScene {
    Albedo albedo;
    Metres sunDistance;
    Irradiance irradiance; // at normal incidence, at sunDistance
    PatchPlacement patch;
    Direction towardSun; // world, unit
};

// --- what the shader reads ----------------------------------------------------

// The light on the surface, in the simulation's own types.
struct LambertLighting {
    Albedo albedo;
    Irradiance irradiance; // at normal incidence, from astro/Sun.hpp's solarIrradianceAt
    Direction surfaceNormal;
    Direction towardSun;
};

// shaders/lambert.vert's and lambert.frag's block, member for member: the
// vertex stage reads the matrix, the fragment stage the rest. Directions are
// vec4 with w = 0, the layout a GLSL vec3 would pad to anyway, written out.
struct LambertPushConstants {
    Mat4f viewProjection{};
    Vec4f surfaceNormal{};
    Vec4f towardSun{};
    f32 albedo{};
    f32 irradiance{}; // W/m^2
};

// The block against the shaders, read from the compiled modules with
// `spirv-cross --reflect` (M1-18): a mat4 at 0, vec4s at 64 and 80, floats at
// 96 and 100, 104 bytes -- inside the 128 every device guarantees.
static_assert(std::is_standard_layout_v<LambertPushConstants> &&
                  sizeof(LambertPushConstants) == 104U &&
                  offsetof(LambertPushConstants, surfaceNormal) == 64U &&
                  offsetof(LambertPushConstants, towardSun) == 80U &&
                  offsetof(LambertPushConstants, albedo) == 96U &&
                  offsetof(LambertPushConstants, irradiance) == 100U,
              "lambert's block: a mat4 at 0, vec4s at 64 and 80, floats at 96 and 100");

// One range for both stages over the whole block: the shape register decision
// 145 set for body's block, and for its reason.
inline constexpr PushConstantRange kLambertPushConstantRange =
    PushConstantRange::wholeBlock<LambertPushConstants>(ShaderStages::VertexAndFragment).value();

// How far from unit length a direction handed to the shader may be: 16 units
// in the last place, the bound core/Math.hpp's kUnitQuaternionTolerance sets
// for a quaternion and for the same reason -- a direction built by a rotation
// or two is a few roundings from unit length, and one further off is a
// defect, not an input.
inline constexpr Tolerance kUnitDirectionTolerance{16.0 * std::numeric_limits<f64>::epsilon()};

// The preconditions of squarePatch and toShaderLambert. **Public, and in the
// header**, for the reason view/Camera.hpp's isNarrowable is: a precondition
// nothing can check from outside is one nobody can test, and a function whose
// only caller is an assertion becomes an unused one where assertions compile
// away.
[[nodiscard]] inline bool isUnitDirection(const Direction& d) noexcept {
    return nearlyEqual(length(d).value(), 1.0, kUnitDirectionTolerance);
}

[[nodiscard]] constexpr bool isFinitePosition(const Position& p) noexcept {
    return isFinite(p.x.value()) && isFinite(p.y.value()) && isFinite(p.z.value());
}

// An irradiance the shader can be handed: finite, not negative, and inside
// 32-bit range, where narrowing it is defined.
[[nodiscard]] constexpr bool isShaderIrradiance(Irradiance irradiance) noexcept {
    const f64 value = irradiance.value();
    return isFinite(value) && value >= 0.0 &&
           value <= static_cast<f64>(std::numeric_limits<f32>::max());
}

// **The light narrowed for the shader: the fifth place in `src/` that narrows
// to 32 bits** (register decision 268), beside view/Camera.hpp's
// toRenderSpace and toShaderMatrix, view/Exposure.hpp's toShaderExposure and
// view/ProbeGradient.hpp's toShaderRamp. An albedo, an irradiance and two
// unit directions: none is a position, so nothing is subtracted first, and a
// float resolves each to 6e-8 of itself -- far inside the binary16 step,
// 2^-11 to 2^-10 of the value, that the HDR target then rounds the radiance
// to. The matrix arrives already narrowed, by toShaderMatrix.
[[nodiscard]] LambertPushConstants toShaderLambert(const LambertLighting& lighting,
                                                   const Mat4f& viewProjection) noexcept;

} // namespace orb::view

#endif // ORBSIM_VIEW_LAMBERT_HPP
