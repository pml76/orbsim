#ifndef ORBSIM_VIEW_EXPOSURE_HPP
#define ORBSIM_VIEW_EXPOSURE_HPP
//
// Manual photographic exposure, and the one line that turns the renderer's
// radiance into something a camera can expose (M1-15; ADR 0014).
//
// **What exposure is for.** The scene is rendered in radiance, and space has
// the most brutal dynamic range of any rendering domain: a sunlit cloud top
// and a star field differ by more than ten orders of magnitude. What makes a
// photograph of it readable is the photographer's choice of aperture, shutter
// time and sensitivity, so the control here is a camera and not a brightness
// slider. Auto-exposure is deferred, and when it lands it must be pinned in
// probe mode (ADR 0014).
//
// **The radiometric-to-photometric convention -- the paragraph every number
// downstream of exposure depends on.**
//
//   * The scene is rendered in **radiance, W/(m^2 sr), per channel**, the
//     channels being linear Rec. 709 (sRGB) primaries. Every shader writes
//     that into the HDR target and nothing else (ADR 0014).
//   * **Luminance** is obtained from the luminance-weighted channel sum,
//     ITU-R BT.709's weights 0.2126, 0.7152, 0.0722 -- the weights of the
//     primaries this renderer and AgX use, and the ones Sobotka's AgX
//     configuration names -- times a **luminous efficacy**.
//   * **The efficacy is that of sunlight, 98.9225 lm/W** (register decision
//     175), derived by scripts/solar-efficacy.py as the SI definition of the
//     candela gives it: 683 lm/W times the CIE 1924 photopic luminous
//     efficiency function, integrated over the TSIS-1 Hybrid Solar Reference
//     Spectrum (Coddington et al. 2021) -- 134 634 lx at 1 AU -- and divided by
//     the 1361 W/m^2 astro/Sun.hpp holds. That is how pbrt-v4 and Bruneton's
//     precomputed atmosphere derive photometric quantities, from the spectrum
//     of the light rather than from a fixed number. Its uncertainty is **0.30 %**
//     at worst, the spectrum's own stated uncertainty taken as fully
//     correlated; the older ASTM E-490 spectrum gives 97.59 lm/W, 1.4 % lower.
//   * **Why not 179 lm/W**, which ADR 0014 first named: that is the convention
//     *Radiance* uses, and Radiance's own source defines it as the efficacy of
//     "equal energy white 380-780nm" (src/common/color.h) -- watts counted
//     over the visible band only. This renderer's radiance comes from the
//     *total* solar irradiance, so 179 would have made every sunlit luminance
//     1.81 times too bright: a hidden 0.86-stop error, which is a tuning
//     constant wearing a unit, the thing ADR 0014 exists to forbid.
//   * **Because the weights sum to one, the multiply is one number for all
//     three channels**: a grey radiance L has luminance efficacy * L. The
//     weights define what "luminance" means; they need not be applied
//     per pixel.
//   * **Its limit, stated so it is not discovered**: the efficacy is
//     sunlight's. Light of another spectrum -- city lights (M1-35), a sodium
//     lamp -- has another efficacy, and whatever adds such a source states it
//     then. Sunlight reflected by Earth's surface and scattered by its
//     atmosphere is not quite the Sun's spectrum either; phase D's
//     atmosphere, which works per wavelength, is where that is refined.
//
// **The exposure relations** are ISO 2720's exposure value and ISO 12232's
// saturation-based sensitivity, as Lagarde & de Rousiers (2014), "Moving
// Frostbite to Physically Based Rendering", section 4.2, derive them and as
// Filament's Exposure.cpp implements them (register decision 177):
//
//     EV100 = log2(N^2 / t * 100 / S)
//     Lmax  = 78 / (S q) * N^2 / t  =  (78 / (100 q)) * 2^EV100
//     exposure factor = 1 / Lmax
//
// with N the f-number, t the shutter time in seconds, S the ISO arithmetic
// speed and q = 0.65 the lens and vignetting attenuation ISO 12232 assumes.
// Lmax is the luminance that just saturates the sensor, so the factor maps it
// to 1.0; a metered 18 % grey card then lands at 12.7 % of saturation, the
// half stop of headroom the 78 was chosen to leave. **78 / 65 is 1.2 exactly**,
// which is why the factor is usually written 1 / (1.2 * 2^EV100).
//
// **None of this is a tuning constant.** Every number above has a source, and
// if a value had to be tweaked until the image looked right, the chain above
// it would be wrong (ADR 0014).
//
// Not constexpr where it calls std::log2 or std::exp2, which are not constant
// expressions before C++26 on every front end here (register decision 99).
//
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace orb::view {

// The ways a camera setting can be asked for and not exist. Reported rather
// than asserted, because a scenario file or a user can produce every one of
// them (ADR 0002). One value per setting, so the caller is told which to fix.
enum class ExposureError : std::uint8_t {
    InvalidAperture,    // an f-number not finite, or not greater than zero
    InvalidShutterTime, // a shutter time not finite, or not greater than zero
    InvalidSensitivity, // an ISO speed not finite, or not greater than zero
};

[[nodiscard]] constexpr std::string_view describe(ExposureError error) noexcept {
    switch (error) {
    case ExposureError::InvalidAperture:
        return "the f-number must be finite and greater than zero";
    case ExposureError::InvalidShutterTime:
        return "the shutter time must be finite and greater than zero";
    case ExposureError::InvalidSensitivity:
        return "the ISO speed must be finite and greater than zero";
    }
    return "unknown exposure error";
}

namespace detail {

// Finite and greater than zero, in the negated form that also refuses a NaN
// (the shape view/Projection.hpp's isFinitePositive keeps, and for its reason).
[[nodiscard]] constexpr bool isUsableSetting(f64 value) noexcept {
    return isFinite(value) && !(value <= 0.0);
}

} // namespace detail

// **Three settings, three types, each validated at construction** (ADR 0022,
// register decision 178). Three adjacent bare doubles would be I.24 again, and
// each has the same physical bound: an f-number, a shutter time and a speed
// are all positive, and none has an upper limit this code should invent.
// **No lower limit beyond zero either**: an f-number below 0.5 cannot be built
// in air, but it is harmless arithmetic, and a threshold with a hidden
// physical assumption is the defect class this project has shipped once.

// The f-number N: focal length over the diameter of the entrance pupil.
class Aperture {
public:
    [[nodiscard]] static constexpr std::expected<Aperture, ExposureError>
    from(f64 fNumber) noexcept {
        if (!detail::isUsableSetting(fNumber)) {
            return std::unexpected(ExposureError::InvalidAperture);
        }
        return Aperture{fNumber};
    }
    [[nodiscard]] constexpr f64 fNumber() const noexcept { return fNumber_; }

private:
    explicit constexpr Aperture(f64 fNumber) noexcept : fNumber_(fNumber) {}
    f64 fNumber_;
};

// How long the sensor is exposed.
class ShutterTime {
public:
    [[nodiscard]] static constexpr std::expected<ShutterTime, ExposureError>
    from(Seconds duration) noexcept {
        if (!detail::isUsableSetting(duration.value())) {
            return std::unexpected(ExposureError::InvalidShutterTime);
        }
        return ShutterTime{duration};
    }
    [[nodiscard]] constexpr Seconds duration() const noexcept { return duration_; }

private:
    explicit constexpr ShutterTime(Seconds duration) noexcept : duration_(duration) {}
    Seconds duration_;
};

// The sensitivity S, as an ISO arithmetic speed: 100, 400, 3200.
class Iso {
public:
    [[nodiscard]] static constexpr std::expected<Iso, ExposureError> from(f64 speed) noexcept {
        if (!detail::isUsableSetting(speed)) {
            return std::unexpected(ExposureError::InvalidSensitivity);
        }
        return Iso{speed};
    }
    [[nodiscard]] constexpr f64 speed() const noexcept { return speed_; }

private:
    explicit constexpr Iso(f64 speed) noexcept : speed_(speed) {}
    f64 speed_;
};

// The three together: what a camera is set to. The members have three
// different types, so they cannot be given in the wrong order.
struct CameraSettings {
    Aperture aperture;
    ShutterTime shutterTime;
    Iso iso;
};

// An exposure value at ISO 100, in stops. A type rather than a bare f64
// (non-negotiable 1): a stop is a base-2 logarithm, and nothing else in this
// project is one. Any finite value is meaningful -- negative for dim scenes --
// so it is not validated; exposureValue100 cannot produce anything else from
// three validated settings.
class ExposureValue100 {
public:
    explicit constexpr ExposureValue100(f64 stops) noexcept : stops_(stops) {}
    [[nodiscard]] constexpr f64 stops() const noexcept { return stops_; }

private:
    f64 stops_;
};

// What exposure multiplies a luminance by: per cd/m^2.
using PerLuminance = Scalar<mp_units::pow<2>(units::kMetre) / mp_units::si::candela>;

// What the resolve pass multiplies a radiance by: a luminous efficacy times an
// exposure factor, which is per W/(m^2 sr). Named by what it acts on.
using PerRadiance = decltype(LuminousEfficacy{} * PerLuminance{});

// The luminous efficacy of sunlight: see the paragraph at the top of this file.
inline constexpr LuminousEfficacy kSunlightLuminousEfficacy{98.9225};

// ISO 12232's saturation-based speed constant, and the lens attenuation q it
// is paired with in Frostbite's derivation. Their quotient over the reference
// speed of 100 is the 1.2 of the usual formula.
inline constexpr f64 kSaturationSpeedConstant = 78.0;
inline constexpr f64 kLensAttenuation = 0.65;
inline constexpr f64 kReferenceSpeed = 100.0; // the "100" in EV100

// EV100 = log2(N^2 / t * 100 / S). One logarithm of one product: the three
// settings enter as a single number, so their rounding is that of four
// multiplications and one log2, and no difference of two logarithms can
// cancel.
[[nodiscard]] inline ExposureValue100 exposureValue100(const CameraSettings& settings) noexcept {
    const f64 n = settings.aperture.fNumber();
    const f64 t = settings.shutterTime.duration().value();
    const f64 s = settings.iso.speed();
    return ExposureValue100{std::log2(n * n / t * (kReferenceSpeed / s))};
}

// The exposure factor, 1 / Lmax with Lmax = (78 / (100 q)) * 2^EV100.
[[nodiscard]] inline PerLuminance exposureFactor(ExposureValue100 ev) noexcept {
    const f64 maximumLuminance =
        kSaturationSpeedConstant / (kReferenceSpeed * kLensAttenuation) * std::exp2(ev.stops());
    return PerLuminance{1.0 / maximumLuminance};
}

// Radiance in, exposed value out: the efficacy and the exposure factor
// together, which is the one number the resolve pass multiplies by.
[[nodiscard]] inline PerRadiance radianceExposure(ExposureValue100 ev) noexcept {
    return kSunlightLuminousEfficacy * exposureFactor(ev);
}

// **The exposure narrowed for the shader: the second place in `src/` that
// narrows to 32 bits** (register decision 179), beside view/Camera.hpp's
// toRenderSpace. `grep static_cast<f32> src/` finds both, and a third is a
// defect. Nothing is subtracted first because nothing needs to be: this is a
// scale factor, typically 1e-6 to 1e-2, where a float's relative precision is
// the same 6e-8 it is everywhere.
//
// The unit comes off here, in the same expression that narrows: the shader
// multiplies a radiance by it, and GLSL has no units.
[[nodiscard]] inline f32 toShaderExposure(PerRadiance exposure) noexcept {
    const f64 value = exposure.value();
    // A factor from three validated settings is finite and positive, and one
    // outside float range would be a defect upstream: the undefined narrowing
    // view/Camera.hpp's isNarrowable describes.
    ORBSIM_EXPECTS(isFinite(value) && value > 0.0 &&
                   value <= static_cast<f64>(std::numeric_limits<f32>::max()));
    return static_cast<f32>(value);
}

// --- compile-time tests -----------------------------------------------------

static_assert(!std::is_constructible_v<Aperture, f64> && !std::is_constructible_v<Iso, f64> &&
                  !std::is_constructible_v<ShutterTime, Seconds>,
              "the only door to a setting is from(), so a bad one has nowhere to live");
static_assert(!std::is_default_constructible_v<CameraSettings>,
              "and there is no default camera setting to fall back on");
static_assert(std::is_trivially_copyable_v<CameraSettings>);
static_assert(Aperture::from(16.0).has_value() && !Aperture::from(0.0).has_value() &&
                  !Aperture::from(-2.8).has_value(),
              "an f-number is positive");
static_assert(Aperture::from(0.0).error() == ExposureError::InvalidAperture,
              "and is refused by its own name");
static_assert(!ShutterTime::from(Seconds{0.0}).has_value() &&
                  ShutterTime::from(Seconds{0.0}).error() == ExposureError::InvalidShutterTime,
              "a shutter time is positive, and says so");
static_assert(!Iso::from(-100.0).has_value() &&
                  Iso::from(-100.0).error() == ExposureError::InvalidSensitivity,
              "so is a speed");
static_assert(describe(ExposureError::InvalidAperture) !=
                  describe(ExposureError::InvalidShutterTime) &&
              describe(ExposureError::InvalidShutterTime) !=
                  describe(ExposureError::InvalidSensitivity));

// 78 / (100 * 0.65) is the 1.2 of the usual formula -- to the rounding of two
// operations, since 0.65 is not a double.
static_assert(nearlyEqual(kSaturationSpeedConstant / (kReferenceSpeed * kLensAttenuation),
                          1.2,
                          Tolerance{4e-16}));

// The dimension system agrees with the paragraph at the top: a radiance
// times this factor is a plain number, the exposed value AgX takes.
static_assert(
    std::is_constructible_v<Scalar<mp_units::one>, decltype(Radiance{1.0} * PerRadiance{1.0})>,
    "a radiance times the resolve pass's factor is dimensionless");
static_assert(
    !std::is_constructible_v<Scalar<mp_units::one>, decltype(Irradiance{1.0} * PerRadiance{1.0})>,
    "and an irradiance times it is not, so the solid angle cannot be dropped");
inline constexpr PerRadiance kHalfLuminanceExposure = kSunlightLuminousEfficacy * PerLuminance{0.5};
static_assert(nearlyEqual(Scalar<mp_units::one>{Radiance{2.0} * kHalfLuminanceExposure}.value(),
                          2.0 * 98.9225 * 0.5,
                          Tolerance{0.0}),
              "and the number is the plain product, with no factor hidden in a unit");

} // namespace orb::view

#endif // ORBSIM_VIEW_EXPOSURE_HPP
