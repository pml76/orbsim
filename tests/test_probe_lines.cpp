//
// The `lines` probe's frame, checked number by number (M1-19, register
// decision 281): where each axis and the square must land, and in what
// colour, read from the HDR dump the probe wrote.
//
// **Where the expected pixels come from: the pinhole camera, worked out here
// from decision 280's words**, and never through view/Mat4.hpp,
// view/Projection.hpp, view/Camera.hpp or the probe's own code (VERIFICATION.md
// rule 2). A point at depth d in front of the camera, x to its right and y
// above its axis, lands (x/d) * (H/2) / tan(fov/2) pixels right of the frame's
// centre and (y/d) * (H/2) / tan(fov/2) above it -- the definition of a
// vertical field of view on a frame H pixels high. A transposed matrix, a sign
// the wrong way or a camera turned the wrong way round moves a point off its
// pixel, and a matrix that is right on one graphics card is right on every
// one: so this check, unlike a golden image, is the same on every card, and it
// is what catches a fault that an approval of one card's golden let through
// (decision 287).
//
// **"Within one pixel"**: the expected pixel and its eight neighbours are
// searched for the colour. A one-pixel line is drawn by Bresenham's rule
// (ADR 0025), which lights one pixel per column or row along the line and
// need not light the pixel the end point falls in, so the colour is looked
// for beside it as well.
//
// **The colour within one binary16 step** (the owner's ruling, 2026-10-04):
// the HDR target holds 16-bit floats, and every radiance here is one exactly
// -- 130, 437 and 1288 are whole numbers below 2048 -- but the GPU
// interpolates the colour along the line in 32 bits, which may land a
// rounding away before the target rounds it. A channel expected to be zero
// must be exactly zero: nothing interpolated from zeros is anything else.
//
#include "HdrDumpFile.hpp"
#include "core/Scalar.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

using namespace orb;
using namespace orb::test;

namespace {

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;

using Channel = HdrDump::Channel;

// A point, in world metres or as a direction. Plain doubles: this file
// deliberately uses none of the project's vector types.
struct Point {
    f64 x{};
    f64 y{};
    f64 z{};
};

[[nodiscard]] Point minus(const Point& a, const Point& b) noexcept {
    return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

[[nodiscard]] f64 dotOf(const Point& a, const Point& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Decision 280's camera, from its words: 3 m from (0.5, 0.5, 0.3), on the
// azimuth halfway between +X and +Y, 30 degrees up, looking at that point,
// with world Z up on screen; a 45-degree vertical field of view.
struct PinholeCamera {
    Point position;
    Point right;   // unit, horizontal
    Point up;      // unit
    Point forward; // unit, toward the point looked at
};

[[nodiscard]] PinholeCamera decision280Camera() {
    const f64 elevation = std::numbers::pi / 6.0;
    const f64 horizontal = std::cos(elevation) / std::numbers::sqrt2;
    // From the point looked at toward the camera.
    const Point outward{.x = horizontal, .y = horizontal, .z = std::sin(elevation)};
    const Point target{.x = 0.5, .y = 0.5, .z = 0.3};
    constexpr f64 kDistance = 3.0;
    // Looking back along `outward`; right is horizontal and to the right of
    // that view, which from the +X +Y side is toward +Y and away from +X; up
    // is perpendicular to both, with a positive Z.
    const Point right{.x = -1.0 / std::numbers::sqrt2, .y = 1.0 / std::numbers::sqrt2, .z = 0.0};
    const Point up{.x = -std::sin(elevation) / std::numbers::sqrt2,
                   .y = -std::sin(elevation) / std::numbers::sqrt2,
                   .z = std::cos(elevation)};
    return {
        .position = {.x = target.x + kDistance * outward.x,
                     .y = target.y + kDistance * outward.y,
                     .z = target.z + kDistance * outward.z},
        .right = right,
        .up = up,
        .forward = {.x = -outward.x, .y = -outward.y, .z = -outward.z},
    };
}

// Where `world` lands, as the pixel holding it.
[[nodiscard]] Pixel pixelOf(const Point& world) {
    const PinholeCamera camera = decision280Camera();
    const Point d = minus(world, camera.position);
    const f64 depth = dotOf(d, camera.forward);
    REQUIRE(depth > 1.0); // in front of the 1 m near plane
    const f64 pixelsPerUnit = (kHeight / 2.0) / std::tan(std::numbers::pi / 8.0);
    const f64 column = kWidth / 2.0 + dotOf(d, camera.right) / depth * pixelsPerUnit;
    const f64 row = kHeight / 2.0 - dotOf(d, camera.up) / depth * pixelsPerUnit;
    REQUIRE(column >= 1.0);
    REQUIRE(column < kWidth - 1.0);
    REQUIRE(row >= 1.0);
    REQUIRE(row < kHeight - 1.0);
    return {.column = static_cast<std::uint32_t>(column), .row = static_cast<std::uint32_t>(row)};
}

// A radiance in W/(m^2 sr) per channel.
struct Radiance3 {
    f64 red{};
    f64 green{};
    f64 blue{};
};

// Register decision 279's radiances, written out here.
constexpr Radiance3 kRed{.red = 437.0, .green = 0.0, .blue = 0.0};
constexpr Radiance3 kGreen{.red = 0.0, .green = 130.0, .blue = 0.0};
constexpr Radiance3 kBlue{.red = 0.0, .green = 0.0, .blue = 1288.0};
constexpr Radiance3 kWhite{.red = 130.0, .green = 130.0, .blue = 130.0};

// One binary16 step at a positive value in binary16's normal range: at
// exponent e it is 2^(e - 10), as tests/test_radiometry.cpp computes it.
[[nodiscard]] f64 halfStep(f64 value) { return std::exp2(std::floor(std::log2(value)) - 10.0); }

[[nodiscard]] bool channelMatches(f32 read, f64 expected) {
    if (expected == 0.0) return bitsOf(read) == bitsOf(0.0F);
    return std::abs(static_cast<f64>(read) - expected) <= halfStep(expected);
}

[[nodiscard]] bool pixelMatches(const HdrDump& dump, Pixel pixel, const Radiance3& expected) {
    return channelMatches(dump.at(pixel, Channel::Red), expected.red) &&
           channelMatches(dump.at(pixel, Channel::Green), expected.green) &&
           channelMatches(dump.at(pixel, Channel::Blue), expected.blue);
}

// The pixel in the 3x3 square around `centre` holding `expected`, if any.
[[nodiscard]] std::optional<Pixel>
findNear(const HdrDump& dump, Pixel centre, const Radiance3& expected) {
    for (std::uint32_t row = centre.row - 1; row <= centre.row + 1; ++row) {
        for (std::uint32_t column = centre.column - 1; column <= centre.column + 1; ++column) {
            const Pixel pixel{.column = column, .row = row};
            if (pixelMatches(dump, pixel, expected)) return pixel;
        }
    }
    return std::nullopt;
}

[[nodiscard]] HdrDump readLinesDump() {
    const std::filesystem::path path = std::filesystem::path{ORBSIM_PROBE_DIR} / "lines.hdr.f32";
    auto dump = readHdrDump(path);
    INFO("reading " << path.string() << ": " << (dump.has_value() ? "ok" : describe(dump.error())));
    REQUIRE(dump.has_value());
    REQUIRE(dump->width() == kWidth);
    REQUIRE(dump->height() == kHeight);
    return *std::move(dump);
}

void requireLineAt(const HdrDump& dump,
                   std::string_view what,
                   const Point& world,
                   const Radiance3& expected) {
    const Pixel pixel = pixelOf(world);
    INFO(what << " at world (" << world.x << ", " << world.y << ", " << world.z
              << ") should be near column " << pixel.column << ", row " << pixel.row << ", reading "
              << dump.at(pixel, Channel::Red) << ", " << dump.at(pixel, Channel::Green) << ", "
              << dump.at(pixel, Channel::Blue) << " there");
    REQUIRE(findNear(dump, pixel, expected).has_value());
}

} // namespace

TEST_CASE("each axis lands where the pinhole camera puts it, in its own colour") {
    const HdrDump dump = readLinesDump();
    // The tips, and the middles -- an end can fall just past the last pixel
    // a line lights, a middle cannot.
    requireLineAt(dump, "the X axis's tip", {.x = 1.0, .y = 0.0, .z = 0.0}, kRed);
    requireLineAt(dump, "the X axis's middle", {.x = 0.5, .y = 0.0, .z = 0.0}, kRed);
    requireLineAt(dump, "the Y axis's tip", {.x = 0.0, .y = 1.0, .z = 0.0}, kGreen);
    requireLineAt(dump, "the Y axis's middle", {.x = 0.0, .y = 0.5, .z = 0.0}, kGreen);
    requireLineAt(dump, "the Z axis's tip", {.x = 0.0, .y = 0.0, .z = 1.0}, kBlue);
    requireLineAt(dump, "the Z axis's middle", {.x = 0.0, .y = 0.0, .z = 0.5}, kBlue);
}

TEST_CASE("the square's corners and sides land where the pinhole camera puts them, in white") {
    const HdrDump dump = readLinesDump();
    constexpr f64 kLow = 0.25;
    constexpr f64 kHigh = 1.25;
    constexpr f64 kMiddle = 0.75;
    const std::array corners{
        Point{.x = kLow, .y = kLow, .z = 0.0},
        Point{.x = kHigh, .y = kLow, .z = 0.0},
        Point{.x = kHigh, .y = kHigh, .z = 0.0},
        Point{.x = kLow, .y = kHigh, .z = 0.0},
    };
    for (const Point& corner : corners)
        requireLineAt(dump, "a corner of the square", corner, kWhite);
    const std::array sides{
        Point{.x = kMiddle, .y = kLow, .z = 0.0},
        Point{.x = kHigh, .y = kMiddle, .z = 0.0},
        Point{.x = kMiddle, .y = kHigh, .z = 0.0},
        Point{.x = kLow, .y = kMiddle, .z = 0.0},
    };
    for (const Point& side : sides)
        requireLineAt(dump, "the middle of a side", side, kWhite);
}

TEST_CASE("the search finds nothing where nothing is drawn, so it can fail") {
    // VERIFICATION.md rule 23: an instrument seen failing. The square's
    // centre is empty, and each axis's colour is absent at another axis's
    // middle -- the place a swapped colour or a transposed matrix would put
    // it.
    const HdrDump dump = readLinesDump();
    const Pixel centre = pixelOf({.x = 0.75, .y = 0.75, .z = 0.0});
    CHECK_FALSE(findNear(dump, centre, kWhite).has_value());
    CHECK(pixelMatches(dump, centre, {.red = 0.0, .green = 0.0, .blue = 0.0}));
    const Pixel xMiddle = pixelOf({.x = 0.5, .y = 0.0, .z = 0.0});
    const Pixel yMiddle = pixelOf({.x = 0.0, .y = 0.5, .z = 0.0});
    CHECK_FALSE(findNear(dump, xMiddle, kGreen).has_value());
    CHECK_FALSE(findNear(dump, yMiddle, kRed).has_value());
    CHECK_FALSE(findNear(dump, xMiddle, kBlue).has_value());
}
