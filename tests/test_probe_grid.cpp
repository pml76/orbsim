//
// The grid probes' frames, checked number by number (M1-20; register
// decisions 319-336): where the grid lands in `grid-400km`, and how far the
// equator and prime meridian's crossing moves between the jitter frames.
//
// **Where the expected pixels come from: the pinhole camera and the Skyfield
// fixture, worked out here**, never through view/Mat4.hpp, view/Camera.hpp,
// view/PlanetaryGrid.hpp, src/astro or the probes' own code (VERIFICATION.md
// rule 2). The cameras are rebuilt from decision 324's words -- a point of the
// celestial sphere below, an altitude, a heading east of north and a pitch
// below the horizontal -- and a point of the Earth is placed by the committed
// Skyfield row the probes' epoch was chosen from
// (data/skyfield/earth-orientation.txt), an implementation of the IAU models
// that is not ERFA's. A point at depth d in front of the camera, x to its
// right and y above its axis, lands (x/d) f pixels right of the frame's
// centre and (y/d) f above it, f = (H/2) / tan(fov/2).
//
// **The movement budget: 0.05 px at 1920x1080, the same angle as 0.0333 px
// in a probe's 1280x720 frame** (register decision 319), between consecutive
// frames, for the crossing at latitude 0, longitude 0.
//
// **How the crossing is found: by fitting a straight line to the equator's
// red pixels and another to the prime meridian's green ones, within 150 px of
// the predicted crossing and outside 4 px of it, and intersecting the two**
// (register decision 320). The task asked for a centroid; a one-pixel line
// lights whole pixels, and the centroid of a small marker moves by about a
// sixth of a pixel when one pixel comes or goes -- measured in simulation on
// 2026-10-04, 0.5 to 1.2 px -- where a fit over a few hundred pixels averages
// that away. **Neither line may lie near a row, a column or a diagonal**,
// where a one-pixel line repeats the same pattern every pixel and a fit
// learns nothing below a pixel: the scenes keep both about 20 and 33 degrees
// from them (decision 324). Measured on the RX 7900 XTX on 2026-10-04 before
// this test was written: the fit followed the 1 km sequence's 0.217 px steps
// to within 0.008 px, and a throwaway run of 41 positions 0.05 m apart to
// within 0.013 px over 76 differences -- under half the budget, the condition
// the owner set for using it. (Both on the row first chosen, 2026-09-29,
// whose scene differs from this one by about 2 px; on the present row the
// fit follows the 1 km steps to within 0.008 px again.)
//
// **What the 400 km sequence can and cannot show** (register decision 321).
// There a 0.25 m step moves the crossing 0.0003 px, so the frames are nearly
// identical, and the check passes without being able to fail for the defect
// camera-relative rendering prevents: narrowing before subtracting is 0.001
// px there (M1-11, decision 120). The 1 km sequence is where it can.
// **Seen failing, 2026-10-04**: `toRenderSpace` changed by hand to narrow
// each operand before subtracting, the frames rendered again, and this suite
// failed -- the crossing's steps off by up to 0.428 px, 12.8 times the
// budget, as simulated beforehand (0.43 px). That needs the epoch decision
// 336 chose: on the row first chosen the same change moved it 0.0016 px and
// passed.
//
#include "FixtureFile.hpp"
#include "HdrDumpFile.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace orb;
using namespace orb::test;

namespace {

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;

// Register decision 319: the task's 0.05 px at 1920x1080, as the same angle
// in a 1280x720 frame.
constexpr f64 kBudgetPixels = 0.05 * 720.0 / 1080.0;

constexpr f64 kRadius = 6'378'137.0; // WGS-84 a, the grid's sphere
constexpr f64 kStepMetres = 0.25;    // the task's camera step
constexpr int kFrames = 5;

using Channel = HdrDump::Channel;

// A point or a direction, in plain doubles: this file uses none of the
// project's vector types.
struct Point {
    f64 x{};
    f64 y{};
    f64 z{};
};

[[nodiscard]] Point operator+(const Point& a, const Point& b) noexcept {
    return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}
[[nodiscard]] Point operator-(const Point& a, const Point& b) noexcept {
    return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}
[[nodiscard]] Point operator*(const Point& a, f64 s) noexcept {
    return {.x = a.x * s, .y = a.y * s, .z = a.z * s};
}

[[nodiscard]] Point unit(const Point& a) noexcept {
    return a * (1.0 / std::sqrt((a.x * a.x) + (a.y * a.y) + (a.z * a.z)));
}

// One of a camera's three unit axes: a type of its own, so that the
// component of a displacement along it takes two different types and the two
// cannot be given the wrong way round (non-negotiable 1).
struct Axis {
    Point direction;
};

[[nodiscard]] f64 along(const Point& displacement, const Axis& axis) noexcept {
    const Point& a = axis.direction;
    return (displacement.x * a.x) + (displacement.y * a.y) + (displacement.z * a.z);
}

[[nodiscard]] f64 radiansOf(f64 degrees) noexcept { return degrees * std::numbers::pi / 180.0; }

// Decision 324's words for a camera: the point straight below, by right
// ascension and declination in the celestial frame; the altitude above the
// sphere; the heading, east of north, north toward the celestial pole; the
// pitch below the horizontal.
struct CameraWords {
    f64 rightAscensionDegrees{};
    f64 declinationDegrees{};
    f64 altitudeMetres{};
    f64 headingDegrees{};
    f64 pitchDegrees{};
};

constexpr CameraWords k400km{
    .rightAscensionDegrees = 44.7137335893,
    .declinationDegrees = -6.1029606260,
    .altitudeMetres = 400e3,
    .headingDegrees = 35.0,
    .pitchDegrees = 27.5,
};
constexpr CameraWords k1km{
    .rightAscensionDegrees = 44.7033267157,
    .declinationDegrees = -0.1029696166,
    .altitudeMetres = 1e3,
    .headingDegrees = 33.0,
    .pitchDegrees = 90.0,
};

struct PinholeCamera {
    Point position;
    Axis right;
    Axis up;
    Axis forward; // the way it looks
};

// The camera of `words`, moved `aside` metres to its own right. Below is the
// unit vector to the point beneath; east is perpendicular to it and to the
// celestial pole, north completes them; the heading turns from north toward
// east, and the pitch tips the view down toward below.
[[nodiscard]] PinholeCamera cameraOf(const CameraWords& words, f64 aside) {
    const f64 ra = radiansOf(words.rightAscensionDegrees);
    const f64 dec = radiansOf(words.declinationDegrees);
    const f64 heading = radiansOf(words.headingDegrees);
    const f64 pitch = radiansOf(words.pitchDegrees);
    const Point below{
        .x = std::cos(dec) * std::cos(ra),
        .y = std::cos(dec) * std::sin(ra),
        .z = std::sin(dec),
    };
    // Pole x below, written out: (0, 0, 1) x (bx, by, bz) = (-by, bx, 0).
    const Point east = unit({.x = -below.y, .y = below.x, .z = 0.0});
    // below x east.
    const Point north{
        .x = (below.y * east.z) - (below.z * east.y),
        .y = (below.z * east.x) - (below.x * east.z),
        .z = (below.x * east.y) - (below.y * east.x),
    };
    const Point level = (north * std::cos(heading)) + (east * std::sin(heading));
    const Point forward = (level * std::cos(pitch)) - (below * std::sin(pitch));
    const Point right = (east * std::cos(heading)) - (north * std::sin(heading));
    // Up completes right, up and back (= -forward) as a right-handed frame:
    // up = back x right = right x forward, written out.
    const Point up{
        .x = (right.y * forward.z) - (right.z * forward.y),
        .y = (right.z * forward.x) - (right.x * forward.z),
        .z = (right.x * forward.y) - (right.y * forward.x),
    };
    return {
        .position = (below * (kRadius + words.altitudeMetres)) + (right * aside),
        .right = {.direction = right},
        .up = {.direction = up},
        .forward = {.direction = forward},
    };
}

// Where `world` lands, in pixels: x rightward and y downward from the
// frame's top-left corner, so that pixel (c, r) covers [c, c + 1) x [r, r + 1)
// and its centre is at (c + 0.5, r + 0.5).
struct ScreenPoint {
    f64 x{};
    f64 y{};
};

[[nodiscard]] ScreenPoint screenOf(const PinholeCamera& camera, const Point& world) {
    const Point d = world - camera.position;
    const f64 depth = along(d, camera.forward);
    REQUIRE(depth > 1.0); // in front of the 1 m near plane
    const f64 f = (kHeight / 2.0) / std::tan(std::numbers::pi / 8.0);
    return {
        .x = (kWidth / 2.0) + (along(d, camera.right) / depth * f),
        .y = (kHeight / 2.0) - (along(d, camera.up) / depth * f),
    };
}

// The Skyfield fixture's celestial-to-terrestrial matrix at the probes'
// epoch: the row at JD 2460886.5 + 0.2699127197265625 TT.
[[nodiscard]] RotationMatrix fixtureMatrix() {
    auto fixture = readEarthOrientation(std::filesystem::path{dataDirectory()} / "skyfield" /
                                        "earth-orientation.txt");
    INFO((fixture.has_value() ? "" : describe(fixture.error())));
    REQUIRE(fixture.has_value());
    const auto tt = TtTime::fromJulianDate({.day = 2'460'886.5, .fraction = 0.2699127197265625});
    REQUIRE(tt.has_value());
    const auto row = std::ranges::find_if(fixture->rows, [&](const RotationAtEpoch& r) {
        return r.tt.modifiedJulianDay() == tt->modifiedJulianDay() &&
               r.tt.picosecondOfDay() == tt->picosecondOfDay();
    });
    REQUIRE(row != fixture->rows.end());
    return row->celestialToTerrestrial;
}

// A point of the sphere at a latitude and longitude, in the celestial frame:
// M^T times its Earth-fixed position.
struct Place {
    f64 latitudeDegrees{};
    f64 longitudeDegrees{};
};

[[nodiscard]] Point worldOf(const RotationMatrix& m, const Place& place) {
    const f64 lat = radiansOf(place.latitudeDegrees);
    const f64 lon = radiansOf(place.longitudeDegrees);
    const Point v{
        .x = kRadius * std::cos(lat) * std::cos(lon),
        .y = kRadius * std::cos(lat) * std::sin(lon),
        .z = kRadius * std::sin(lat),
    };
    const auto& r = m.rows;
    return {
        .x = (r.at(0).at(0) * v.x) + (r.at(1).at(0) * v.y) + (r.at(2).at(0) * v.z),
        .y = (r.at(0).at(1) * v.x) + (r.at(1).at(1) * v.y) + (r.at(2).at(1) * v.z),
        .z = (r.at(0).at(2) * v.x) + (r.at(1).at(2) * v.y) + (r.at(2).at(2) * v.z),
    };
}

[[nodiscard]] HdrDump readDump(std::string_view probe) {
    const std::filesystem::path path =
        std::filesystem::path{ORBSIM_PROBE_DIR} / (std::string(probe) + ".hdr.f32");
    auto dump = readHdrDump(path);
    INFO("reading " << path.string() << ": " << (dump.has_value() ? "ok" : describe(dump.error())));
    REQUIRE(dump.has_value());
    REQUIRE(dump->width() == kWidth);
    REQUIRE(dump->height() == kHeight);
    return *std::move(dump);
}

// --- colours ---------------------------------------------------------------

// A radiance in W/(m^2 sr) per channel: decision 327's, written out here.
struct Radiance3 {
    f64 red{};
    f64 green{};
    f64 blue{};
};

constexpr Radiance3 kRed{.red = 437.0, .green = 0.0, .blue = 0.0};   // the equator
constexpr Radiance3 kGreen{.red = 0.0, .green = 130.0, .blue = 0.0}; // the prime meridian
constexpr Radiance3 kBlue{.red = 0.0, .green = 0.0, .blue = 1288.0}; // the horizon, decision 338

// One binary16 step at a positive value in binary16's normal range, as
// tests/test_probe_lines.cpp allows for the GPU's interpolation.
[[nodiscard]] f64 halfStep(f64 value) { return std::exp2(std::floor(std::log2(value)) - 10.0); }

// A channel as read beside what it should be. A struct rather than an f32
// and an f64 in a row, which convert into each other (non-negotiable 1).
struct ChannelReading {
    f32 read{};
    f64 expected{};
};

[[nodiscard]] bool channelMatches(const ChannelReading& channel) {
    if (channel.expected == 0.0) return bitsOf(channel.read) == bitsOf(0.0F);
    return std::abs(static_cast<f64>(channel.read) - channel.expected) <=
           halfStep(channel.expected);
}

[[nodiscard]] bool pixelMatches(const HdrDump& dump, Pixel pixel, const Radiance3& expected) {
    return channelMatches({.read = dump.at(pixel, Channel::Red), .expected = expected.red}) &&
           channelMatches({.read = dump.at(pixel, Channel::Green), .expected = expected.green}) &&
           channelMatches({.read = dump.at(pixel, Channel::Blue), .expected = expected.blue});
}

[[nodiscard]] Pixel pixelHolding(const ScreenPoint& point) {
    REQUIRE(point.x >= 1.0);
    REQUIRE(point.x < kWidth - 1.0);
    REQUIRE(point.y >= 1.0);
    REQUIRE(point.y < kHeight - 1.0);
    return {
        .column = static_cast<std::uint32_t>(point.x),
        .row = static_cast<std::uint32_t>(point.y),
    };
}

// Whether `expected` is in the 3x3 pixels around the one holding `point`.
[[nodiscard]] bool
foundNear(const HdrDump& dump, const ScreenPoint& point, const Radiance3& expected) {
    const Pixel centre = pixelHolding(point);
    for (std::uint32_t row = centre.row - 1; row <= centre.row + 1; ++row) {
        for (std::uint32_t column = centre.column - 1; column <= centre.column + 1; ++column) {
            if (pixelMatches(dump, {.column = column, .row = row}, expected)) return true;
        }
    }
    return false;
}

// --- the line fit ------------------------------------------------------------

// A line through `point` along the unit vector `along`.
struct Line {
    ScreenPoint point;
    ScreenPoint along;
};

// The pixels of one colour within `window` px of `centre` and outside `gap`
// px of it: the colour's channels that are zero exactly zero, the others
// positive -- so the crossing pixel and any grey pixel are left out.
struct Region {
    ScreenPoint centre;
    f64 window{};
    f64 gap{};
};

[[nodiscard]] std::vector<ScreenPoint>
pixelsOf(const HdrDump& dump, const Region& region, const Radiance3& colour) {
    std::vector<ScreenPoint> found;
    const auto lit = [](f32 value, f64 expected) {
        return expected == 0.0 ? bitsOf(value) == bitsOf(0.0F) : value > 0.0F;
    };
    for (std::uint32_t row = 0; row < kHeight; ++row) {
        for (std::uint32_t column = 0; column < kWidth; ++column) {
            const ScreenPoint centre{.x = column + 0.5, .y = row + 0.5};
            const f64 distance = std::hypot(centre.x - region.centre.x, centre.y - region.centre.y);
            if (distance > region.window || distance <= region.gap) continue;
            const Pixel pixel{.column = column, .row = row};
            if (lit(dump.at(pixel, Channel::Red), colour.red) &&
                lit(dump.at(pixel, Channel::Green), colour.green) &&
                lit(dump.at(pixel, Channel::Blue), colour.blue)) {
                found.push_back(centre);
            }
        }
    }
    return found;
}

// The line through `points` that minimises the sum of squared perpendicular
// distances: through their mean, along the principal axis of their spread,
// whose angle is half atan2(2 Sxy, Sxx - Syy).
[[nodiscard]] Line fitLine(const std::vector<ScreenPoint>& points) {
    REQUIRE(points.size() >= 20); // a line, not a few stray pixels
    const auto n = static_cast<f64>(points.size());
    ScreenPoint mean{};
    for (const ScreenPoint& p : points) {
        mean.x += p.x / n;
        mean.y += p.y / n;
    }
    f64 sxx = 0.0;
    f64 syy = 0.0;
    f64 sxy = 0.0;
    for (const ScreenPoint& p : points) {
        sxx += (p.x - mean.x) * (p.x - mean.x);
        syy += (p.y - mean.y) * (p.y - mean.y);
        sxy += (p.x - mean.x) * (p.y - mean.y);
    }
    const f64 angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    return {.point = mean, .along = {.x = std::cos(angle), .y = std::sin(angle)}};
}

// Where two lines cross: p + t d = q + s e, solved for t by Cramer's rule.
struct LinePair {
    Line first;
    Line second;
};

[[nodiscard]] ScreenPoint crossingOf(const LinePair& lines) {
    const Line& a = lines.first;
    const Line& b = lines.second;
    const f64 determinant = (b.along.x * a.along.y) - (a.along.x * b.along.y);
    REQUIRE(std::abs(determinant) > 0.5); // the two lines are far from parallel
    const f64 dx = b.point.x - a.point.x;
    const f64 dy = b.point.y - a.point.y;
    const f64 t = ((b.along.x * dy) - (b.along.y * dx)) / determinant;
    return {.x = a.point.x + (t * a.along.x), .y = a.point.y + (t * a.along.y)};
}

// The crossing of the equator and the prime meridian as the frame shows it,
// looked for around where the pinhole camera puts it.
[[nodiscard]] ScreenPoint measuredCrossing(const HdrDump& dump, const ScreenPoint& predicted) {
    constexpr f64 kWindowPixels = 150.0;
    constexpr f64 kGapPixels = 4.0;
    const Region region{.centre = predicted, .window = kWindowPixels, .gap = kGapPixels};
    return crossingOf({
        .first = fitLine(pixelsOf(dump, region, kRed)),
        .second = fitLine(pixelsOf(dump, region, kGreen)),
    });
}

// Every step of a sequence: the crossing's movement as measured and as the
// pinhole camera predicts it, and the difference between the two.
struct Step {
    f64 predicted{};
    f64 measured{};
    f64 difference{};
};

[[nodiscard]] std::vector<Step> stepsOf(std::string_view prefix, const CameraWords& words) {
    const Point crossing =
        worldOf(fixtureMatrix(), {.latitudeDegrees = 0.0, .longitudeDegrees = 0.0});
    std::vector<ScreenPoint> predicted;
    std::vector<ScreenPoint> measured;
    for (int frame = 0; frame < kFrames; ++frame) {
        const ScreenPoint expected = screenOf(cameraOf(words, frame * kStepMetres), crossing);
        predicted.push_back(expected);
        measured.push_back(
            measuredCrossing(readDump(std::string(prefix) + std::to_string(frame)), expected));
    }
    std::vector<Step> steps;
    for (std::size_t i = 0; i + 1 < predicted.size(); ++i) {
        const ScreenPoint p{
            .x = predicted.at(i + 1).x - predicted.at(i).x,
            .y = predicted.at(i + 1).y - predicted.at(i).y,
        };
        const ScreenPoint m{
            .x = measured.at(i + 1).x - measured.at(i).x,
            .y = measured.at(i + 1).y - measured.at(i).y,
        };
        steps.push_back({
            .predicted = std::hypot(p.x, p.y),
            .measured = std::hypot(m.x, m.y),
            .difference = std::hypot(m.x - p.x, m.y - p.y),
        });
    }
    return steps;
}

void requireStepsWithinBudget(std::string_view prefix, const std::vector<Step>& steps) {
    std::size_t from = 0;
    for (const Step& step : steps) {
        INFO(prefix << from << " to " << prefix << from + 1 << ": predicted " << step.predicted
                    << " px, measured " << step.measured << " px, differing by " << step.difference
                    << " px against a budget of " << kBudgetPixels);
        CHECK(step.difference <= kBudgetPixels);
        ++from;
    }
}

// A point of the horizon seen from `camera`, from its definition: the
// points P with |P| = R and P . E = R^2, E the camera's position, which form
// the circle of radius R sqrt(1 - (R/|E|)^2) centred at (R/|E|)^2 E. Placed
// here by `degrees` from the direction the camera faces, around E -- a
// different construction from the code's, on the same circle.
[[nodiscard]] Point horizonPoint(const PinholeCamera& camera, f64 degrees) {
    const Point& e = camera.position;
    const f64 d = std::sqrt((e.x * e.x) + (e.y * e.y) + (e.z * e.z));
    const Point toEye = e * (1.0 / d);
    const Point& f = camera.forward.direction;
    const f64 fe = (f.x * toEye.x) + (f.y * toEye.y) + (f.z * toEye.z);
    const Point ahead = unit(f - (toEye * fe));
    // toEye x ahead, written out.
    const Point side{
        .x = (toEye.y * ahead.z) - (toEye.z * ahead.y),
        .y = (toEye.z * ahead.x) - (toEye.x * ahead.z),
        .z = (toEye.x * ahead.y) - (toEye.y * ahead.x),
    };
    const f64 ratio = (kRadius / d) * (kRadius / d);
    const f64 radius = kRadius * std::sqrt(1.0 - ratio);
    const f64 a = radiansOf(degrees);
    return (e * ratio) + (ahead * (radius * std::cos(a))) + (side * (radius * std::sin(a)));
}

} // namespace

// --- grid-400km ---------------------------------------------------------------

TEST_CASE(
    "grid-400km: the equator, the prime meridian and the grid land where Skyfield puts them") {
    // Three places, none symmetric to another, so a mirror, a transposed
    // rotation or a wrong date moves at least one off its pixel: the crossing
    // (green, the prime meridian being drawn last), the equator at 15 degrees
    // east (red, drawn over the grey meridian there), and the prime meridian
    // at 10 degrees north (green, over the grey parallel).
    const HdrDump dump = readDump("grid-400km");
    const RotationMatrix m = fixtureMatrix();
    const PinholeCamera camera = cameraOf(k400km, 0.0);
    struct Expected {
        std::string_view what;
        Place place;
        Radiance3 colour;
    };
    const std::array expectations{
        Expected{
            .what = "latitude 0, longitude 0",
            .place = {.latitudeDegrees = 0.0, .longitudeDegrees = 0.0},
            .colour = kGreen,
        },
        Expected{
            .what = "latitude 0, longitude 15 east",
            .place = {.latitudeDegrees = 0.0, .longitudeDegrees = 15.0},
            .colour = kRed,
        },
        Expected{
            .what = "latitude 10 north, longitude 0",
            .place = {.latitudeDegrees = 10.0, .longitudeDegrees = 0.0},
            .colour = kGreen,
        },
    };
    for (const Expected& expected : expectations) {
        const ScreenPoint point = screenOf(camera, worldOf(m, expected.place));
        INFO(expected.what << " should be near (" << point.x << ", " << point.y << ")");
        CHECK(foundNear(dump, point, expected.colour));
    }
}

TEST_CASE("grid-400km: the horizon lands where the sphere's limb is, in blue") {
    // Straight ahead and 15 degrees either side: the limb across the upper
    // third of the frame, where decision 324's pitch puts it.
    const HdrDump dump = readDump("grid-400km");
    const PinholeCamera camera = cameraOf(k400km, 0.0);
    for (const f64 degrees : {-15.0, 0.0, 15.0}) {
        const ScreenPoint point = screenOf(camera, horizonPoint(camera, degrees));
        INFO("the horizon " << degrees << " degrees from ahead should be near (" << point.x << ", "
                            << point.y << ")");
        CHECK(foundNear(dump, point, kBlue));
    }
}

TEST_CASE("grid-400km: the search finds nothing where the colours would be swapped") {
    // VERIFICATION.md rule 23: the instrument seen failing. The prime
    // meridian's green is absent at the equator's 15 degrees east, the
    // equator's red at the prime meridian's 10 degrees north, and the
    // horizon's blue at the crossing.
    const HdrDump dump = readDump("grid-400km");
    const RotationMatrix m = fixtureMatrix();
    const PinholeCamera camera = cameraOf(k400km, 0.0);
    CHECK_FALSE(
        foundNear(dump,
                  screenOf(camera, worldOf(m, {.latitudeDegrees = 0.0, .longitudeDegrees = 15.0})),
                  kGreen));
    CHECK_FALSE(
        foundNear(dump,
                  screenOf(camera, worldOf(m, {.latitudeDegrees = 10.0, .longitudeDegrees = 0.0})),
                  kRed));
    // And the horizon's blue is absent at the crossing, far below the limb.
    CHECK_FALSE(
        foundNear(dump,
                  screenOf(camera, worldOf(m, {.latitudeDegrees = 0.0, .longitudeDegrees = 0.0})),
                  kBlue));
}

// --- the jitter sequences -----------------------------------------------------

TEST_CASE("from 400 km the crossing moves between frames as the pinhole camera predicts") {
    requireStepsWithinBudget("grid-jitter-", stepsOf("grid-jitter-", k400km));
}

TEST_CASE("from 1 km the crossing moves between frames as the pinhole camera predicts") {
    const std::vector<Step> steps = stepsOf("grid-jitter-1km-", k1km);
    requireStepsWithinBudget("grid-jitter-1km-", steps);
    // And the instrument sees movement of that size at all: each step is
    // 0.217 px, six times the budget, so a frame that did not move would fail
    // the check above rather than pass it (VERIFICATION.md rule 23).
    for (const Step& step : steps) {
        CHECK(step.predicted > 6.0 * kBudgetPixels);
    }
}
