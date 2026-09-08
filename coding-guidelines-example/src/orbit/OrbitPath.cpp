#include "orbit/OrbitPath.hpp" // [S14] SF.5: own header, first

#include "core/Contract.hpp"
#include "core/Units.hpp"
#include "core/Vec3.hpp"
#include "orbit/Kepler.hpp"

#include <cmath>
#include <cstddef>
#include <expected>
#include <ranges>
#include <utility>
#include <vector>

namespace orbex {
namespace {

// [S16] Two points make a line, not a shape. Three is the smallest polygon.
constexpr std::size_t kMinSamples = 3;

[[nodiscard]] Vec3 rotateAboutZ(const Vec3& v, Radians angle) noexcept {
    const f64 c = std::cos(angle.value);
    const f64 s = std::sin(angle.value);
    return Vec3{.x = (v.x * c) - (v.y * s), .y = (v.x * s) + (v.y * c), .z = v.z};
}

[[nodiscard]] Vec3 rotateAboutX(const Vec3& v, Radians angle) noexcept {
    const f64 c = std::cos(angle.value);
    const f64 s = std::sin(angle.value);
    return Vec3{.x = v.x, .y = (v.y * c) - (v.z * s), .z = (v.y * s) + (v.z * c)};
}

[[nodiscard]] Radians eccentricToTrueAnomaly(Radians eccentricAnomaly, Eccentricity ecc) noexcept {
    ORBEX_EXPECTS(ecc.value >= 0.0 && ecc.value < 1.0);

    const f64 e = ecc.value;
    const f64 half = eccentricAnomaly.value * 0.5;

    // atan2 rather than atan(tan(...)): the half-angle form stays finite across
    // the whole revolution, where tan(E/2) blows up at E = pi.
    return Radians{
        2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(half), std::sqrt(1.0 - e) * std::cos(half))};
}

[[nodiscard]] Vec3 positionAt(const Elements& elements, Radians trueAnomaly) noexcept {
    const f64 e = elements.eccentricity.value;
    const f64 semiLatusRectum = elements.semiMajorAxis.value * (1.0 - (e * e));
    const f64 nu = trueAnomaly.value;
    const f64 radius = semiLatusRectum / (1.0 + (e * std::cos(nu)));

    ORBEX_EXPECTS(semiLatusRectum > 0.0);
    ORBEX_ENSURES(radius > 0.0);

    // Perifocal frame: x toward periapsis, z along the angular-momentum vector.
    const Vec3 perifocal{.x = radius * std::cos(nu), .y = radius * std::sin(nu), .z = 0.0};

    // Perifocal -> inertial is Rz(node) * Rx(inclination) * Rz(argument),
    // applied right to left.
    const Vec3 inPlane = rotateAboutZ(perifocal, elements.periapsisArgument);
    const Vec3 tilted = rotateAboutX(inPlane, elements.inclination);
    return rotateAboutZ(tilted, elements.ascendingNode);
}

// [S17] Extracted so that sample() stays one screen long and reads as a
// sequence of decisions rather than a wall of arithmetic. F.2: a function does
// one thing.
[[nodiscard]] std::expected<Radians, PathError>
anomalyForSample(const Elements& elements, Radians sweep, Spacing spacing) {
    // Uniform in angle steps the eccentric anomaly directly.
    if (spacing == Spacing::UniformInAngle) return sweep;

    // Uniform in time steps the *mean* anomaly, which needs Kepler's equation
    // solved -- and that can fail, so the failure is propagated rather than
    // swallowed.
    const std::expected<Radians, KeplerError> solved = solveKepler(sweep, elements.eccentricity);
    if (!solved) return std::unexpected(PathError::SolverFailed);
    return *solved;
}

} // namespace

std::expected<OrbitPath, PathError>
OrbitPath::sample(const Elements& elements, GravParam mu, const PathOptions& options) {
    // [S2] Preconditions checked and reported, never silently repaired. A
    // silently repaired input is a spacecraft that mysteriously stops moving
    // three hours into a flight for reasons nobody can reconstruct.
    // [S11] Negated comparisons again, so NaN is rejected rather than admitted.
    if (!(elements.eccentricity.value >= 0.0) || !(elements.eccentricity.value < 1.0)) {
        return std::unexpected(PathError::NotAClosedOrbit);
    }
    if (!(elements.semiMajorAxis.value > 0.0)) {
        return std::unexpected(PathError::NonPositiveSemiMajorAxis);
    }
    if (!(mu.value > 0.0)) return std::unexpected(PathError::NonPositiveGravity);
    if (options.samples.value < kMinSamples) return std::unexpected(PathError::TooFewSamples);

    const std::size_t steps = options.samples.value;
    const std::size_t count =
        steps + (options.closure == PathClosure::ClosedLoop ? std::size_t{1} : std::size_t{0});

    std::vector<Vec3> points;
    // [S10] Not an optimization, just not being wasteful when the size is
    // sitting right there. [S20] It is also the only allocation, made up front
    // rather than inside the loop.
    points.reserve(count);

    // [S9] A range-for over a view, not an index loop with a hand-written
    // bound. The named view cannot have an off-by-one error because it has no
    // `+ 1` in it anywhere.
    for (const std::size_t index : std::views::iota(std::size_t{0}, count)) {
        const f64 fraction = static_cast<f64>(index) / static_cast<f64>(steps);
        const Radians sweep{kTau * fraction};

        // [S16] Stepped in eccentric anomaly, never in true anomaly. Equal
        // steps of true anomaly crowd the samples around periapsis and leave
        // the apoapsis arc as one long straight chord, which looks visibly
        // wrong on an eccentric orbit. Nothing below states that, so without
        // this note somebody "simplifies" it and the bug takes a week to find.
        const std::expected<Radians, PathError> anomaly =
            anomalyForSample(elements, sweep, options.spacing);
        if (!anomaly) return std::unexpected(anomaly.error());

        points.push_back(
            positionAt(elements, eccentricToTrueAnomaly(*anomaly, elements.eccentricity)));
    }

    ORBEX_ENSURES(points.size() == count);

    // Kepler's third law, T = tau * sqrt(a^3 / mu).
    const f64 a = elements.semiMajorAxis.value;
    const Seconds period{kTau * std::sqrt((a * a * a) / mu.value)};

    // [S10] Moved into the returned object, never copied.
    return OrbitPath{std::move(points), period};
}

} // namespace orbex
