//
// Tests for view/ImageCompare.hpp: the golden-image comparison (M1-17; ADR
// 0008; register decisions 228-231).
//
// **Where the expected numbers come from.** Not from this code. The
// straight-segment cases are whole-number means worked out by hand below; the
// 4x4 pattern's averages come from scripts/downsample-reference.py, which
// evaluates the sRGB standard in 50-digit decimal arithmetic (the definition
// scripts/srgb-reference.py already holds the sRGB suite to), and its output
// is pasted below unedited. Every one of those cases lies at least 3.3e-3 of a
// step from a rounding boundary, printed by the script, so double arithmetic
// cannot fall on the other side of one; the one exact tie among them is on
// the straight segment, which the code computes in whole numbers.
//
// **Each tolerance is shown to catch something the other misses** --
// otherwise one of them is decoration (VERIFICATION.md rule 23): one value
// 5 steps out fails the cap and passes the mean; every value 1 step out
// passes the cap and fails the mean. Both boundaries are asserted exactly.
//
#include "view/ImageCompare.hpp"
#include "view/ProbeImage.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace orb::view;

namespace {

// The golden size and the probe frame's, ADR 0008.
constexpr ImageSize kGoldenSize{.width = 640, .height = 360};

// An image built from values the test knows to be valid; a refusal here is a
// defect in the test, and says so.
[[nodiscard]] Rgb8Image imageOf(ImageSize size, std::vector<std::uint8_t> rgb) {
    auto image = Rgb8Image::from(size, std::move(rgb));
    INFO((image.has_value() ? std::string("built") : std::string(describe(image.error()))));
    REQUIRE(image.has_value());
    return *std::move(image);
}

[[nodiscard]] Rgb8Image uniform(ImageSize size, std::uint8_t value) {
    return imageOf(size, std::vector<std::uint8_t>(pixelCount(size) * kRgbChannels, value));
}

// Every value of an image, copied out, for comparing whole images.
[[nodiscard]] std::vector<std::uint8_t> valuesOf(const Rgb8Image& image) {
    std::vector<std::uint8_t> values(image.valueCount());
    for (std::size_t i = 0; i < values.size(); ++i) {
        values.at(i) = image.value(i);
    }
    return values;
}

// A mid-grey image whose values vary, so that a comparison that reads the
// wrong value, or the same one twice, cannot pass by accident.
[[nodiscard]] std::vector<std::uint8_t> patternValues(ImageSize size) {
    std::vector<std::uint8_t> values(pixelCount(size) * kRgbChannels);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values.at(i) = static_cast<std::uint8_t>(64 + (i * 37 % 128));
    }
    return values;
}

} // namespace

// --- building an image ---------------------------------------------------------

TEST_CASE("an image whose values do not fill it is refused by name") {
    const auto tooFew = Rgb8Image::from({.width = 2, .height = 2}, std::vector<std::uint8_t>(11));
    REQUIRE(!tooFew.has_value());
    REQUIRE(tooFew.error() == ImageCompareError::WrongValueCount);

    const auto empty = Rgb8Image::from({.width = 0, .height = 2}, {});
    REQUIRE(!empty.has_value());
    REQUIRE(empty.error() == ImageCompareError::EmptyImage);
}

TEST_CASE("a frame read back as RGBA keeps R, G and B in order and drops alpha") {
    const std::vector<std::uint8_t> rgba{1, 2, 3, 250, 4, 5, 6, 251};
    const Rgb8Image image = Rgb8Image::fromRgba({.width = 2, .height = 1}, rgba);
    REQUIRE(image.size().width == 2);
    REQUIRE(image.size().height == 1);
    REQUIRE(valuesOf(image) == std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6});
}

// --- the box filter, in linear light (decision 228) -----------------------------

TEST_CASE("a 2x2 block of one value averages to that value exactly, for all 256") {
    for (unsigned v = 0; v < 256; ++v) {
        const auto value = static_cast<std::uint8_t>(v);
        INFO("value " << v);
        REQUIRE(averageInLinearLight(std::to_array<std::uint8_t>({value, value, value, value})) ==
                value);
    }
}

TEST_CASE("on the straight segment the average is the mean of the four, and a tie rounds up") {
    // Worked by hand: 0 to 10 are on the sRGB curve's straight segment, where
    // the linear-light average is exactly the mean of the stored values.
    CHECK(averageInLinearLight(std::to_array<std::uint8_t>({0, 0, 0, 1})) == 0); // 0.25
    CHECK(averageInLinearLight(std::to_array<std::uint8_t>({0, 0, 1, 1})) == 1); // 0.5, a tie, up
    CHECK(averageInLinearLight(std::to_array<std::uint8_t>({0, 0, 0, 2})) == 1); // 0.5, a tie, up
    CHECK(averageInLinearLight(std::to_array<std::uint8_t>({0, 1, 1, 1})) == 1); // 0.75
    CHECK(averageInLinearLight(std::to_array<std::uint8_t>({9, 9, 10, 10})) ==
          10);                                                                    // 9.5, a tie, up
    CHECK(averageInLinearLight(std::to_array<std::uint8_t>({3, 7, 9, 10})) == 7); // 7.25
}

TEST_CASE("black beside white averages to 188, as light does, not to 128") {
    // From scripts/downsample-reference.py: 187.516 steps exactly. The mean of
    // the stored values would be 127.5 -- the reason the average is taken in
    // linear light (decision 228).
    REQUIRE(averageInLinearLight(std::to_array<std::uint8_t>({0, 0, 255, 255})) == 188);
}

TEST_CASE("a known 4x4 image halves to the 50-digit reference's averages") {
    // The pattern of scripts/downsample-reference.py, as four 2x2 blocks. Its
    // output, pasted unedited:
    //
    // top-left: {188, 147, 7}
    // top-right: {76, 145, 10}
    // bottom-left: {254, 2, 125}
    // bottom-right: {77, 0, 155}
    //
    // Laid out here as the image's rows, top to bottom, each pixel R, G, B.
    const std::vector<std::uint8_t> rgb{
        // row 0: top-left's top pair, then top-right's
        0,
        11,
        3,
        0,
        12,
        7,
        128,
        250,
        10,
        64,
        5,
        10,
        // row 1: top-left's bottom pair, then top-right's
        255,
        200,
        9,
        255,
        201,
        10,
        32,
        100,
        10,
        16,
        60,
        11,
        // row 2: bottom-left's top pair, then bottom-right's
        255,
        1,
        90,
        254,
        1,
        180,
        77,
        0,
        200,
        77,
        0,
        30,
        // row 3: bottom-left's bottom pair, then bottom-right's
        253,
        2,
        45,
        252,
        2,
        135,
        77,
        0,
        220,
        77,
        1,
        10,
    };
    const auto halved = halveInLinearLight(imageOf({.width = 4, .height = 4}, rgb));
    REQUIRE(halved.has_value());
    REQUIRE(halved->size().width == 2);
    REQUIRE(halved->size().height == 2);
    REQUIRE(valuesOf(*halved) ==
            std::vector<std::uint8_t>{188, 147, 7, 76, 145, 10, 254, 2, 125, 77, 0, 155});
}

TEST_CASE("an image with an odd width or height is refused, not halved") {
    const auto oddWidth = halveInLinearLight(uniform({.width = 3, .height = 2}, 9));
    REQUIRE(!oddWidth.has_value());
    REQUIRE(oddWidth.error() == ImageCompareError::OddDimensions);
    const auto oddHeight = halveInLinearLight(uniform({.width = 2, .height = 3}, 9));
    REQUIRE(!oddHeight.has_value());
    REQUIRE(oddHeight.error() == ImageCompareError::OddDimensions);
}

// --- the two tolerances (decision 229) -----------------------------------------

TEST_CASE("identical images differ by nothing and match") {
    const Rgb8Image frame = imageOf(kGoldenSize, patternValues(kGoldenSize));
    const GoldenImage golden{imageOf(kGoldenSize, patternValues(kGoldenSize))};
    const auto difference = measureDifference(frame, golden);
    REQUIRE(difference.has_value());
    REQUIRE(difference->largest == 0);
    REQUIRE(difference->sum == 0);
    REQUIRE(difference->count == pixelCount(kGoldenSize) * kRgbChannels);
    REQUIRE(matchesGolden(*difference));
}

TEST_CASE("one value 5 steps out fails the cap and passes the mean") {
    std::vector<std::uint8_t> values = patternValues(kGoldenSize);
    const Rgb8Image frame = imageOf(kGoldenSize, values);
    values.at(12'345) = static_cast<std::uint8_t>(values.at(12'345) + 5);
    const auto difference = measureDifference(frame, GoldenImage{imageOf(kGoldenSize, values)});
    REQUIRE(difference.has_value());
    REQUIRE(difference->largest == 5);
    REQUIRE(difference->sum == 5);
    REQUIRE(!largestWithinTolerance(*difference));
    REQUIRE(meanWithinTolerance(*difference));
    REQUIRE(!matchesGolden(*difference));
}

TEST_CASE("every value 1 step out passes the cap and fails the mean") {
    const std::vector<std::uint8_t> values = patternValues(kGoldenSize);
    std::vector<std::uint8_t> shifted = values;
    for (std::uint8_t& v : shifted) {
        v = static_cast<std::uint8_t>(v + 1); // the pattern tops out at 191
    }
    const auto difference =
        measureDifference(imageOf(kGoldenSize, shifted), GoldenImage{imageOf(kGoldenSize, values)});
    REQUIRE(difference.has_value());
    REQUIRE(difference->largest == 1);
    REQUIRE(difference->sum == difference->count);
    REQUIRE(largestWithinTolerance(*difference));
    REQUIRE(!meanWithinTolerance(*difference));
    REQUIRE(!matchesGolden(*difference));
}

TEST_CASE("the cap passes 4 steps and fails 5, in either direction") {
    const ImageSize size{.width = 1, .height = 1};
    const GoldenImage golden{imageOf(size, {100, 100, 100})};
    const auto four = measureDifference(imageOf(size, {104, 100, 100}), golden);
    const auto minusFive = measureDifference(imageOf(size, {100, 95, 100}), golden);
    REQUIRE(four.has_value());
    REQUIRE(minusFive.has_value());
    REQUIRE(four->largest == 4);
    REQUIRE(largestWithinTolerance(*four));
    REQUIRE(minusFive->largest == 5);
    REQUIRE(!largestWithinTolerance(*minusFive));
}

TEST_CASE("the mean must be under half a step: exactly half fails") {
    // Two pixels, six values. A sum of 2 is a mean of 1/3; a sum of 3 is a
    // mean of exactly 1/2, which is not under it.
    const ImageSize size{.width = 2, .height = 1};
    const GoldenImage golden{imageOf(size, {50, 50, 50, 50, 50, 50})};
    const auto third = measureDifference(imageOf(size, {51, 50, 50, 50, 51, 50}), golden);
    const auto half = measureDifference(imageOf(size, {51, 50, 49, 50, 50, 51}), golden);
    REQUIRE(third.has_value());
    REQUIRE(half.has_value());
    REQUIRE(third->sum == 2);
    REQUIRE(meanWithinTolerance(*third));
    REQUIRE(half->sum == 3);
    REQUIRE(!meanWithinTolerance(*half));
}

TEST_CASE("images of different sizes are reported by name, not read out of bounds") {
    const Rgb8Image frame = uniform(kGoldenSize, 7);
    const GoldenImage shorter{uniform({.width = 640, .height = 359}, 7)};
    const GoldenImage narrower{uniform({.width = 639, .height = 360}, 7)};
    for (const GoldenImage* golden : {&shorter, &narrower}) {
        const auto difference = measureDifference(frame, *golden);
        REQUIRE(!difference.has_value());
        REQUIRE(difference.error() == ImageCompareError::DimensionsDiffer);
        const auto diff = differenceImage(frame, *golden);
        REQUIRE(!diff.has_value());
        REQUIRE(diff.error() == ImageCompareError::DimensionsDiffer);
    }
}

TEST_CASE("the two measurements are written out beside their limits") {
    const ImageDifference over{.largest = 8, .sum = 6'144, .count = 691'200};
    REQUIRE(describeDifference(over) ==
            "largest difference 8/255, over the limit of 4/255\n"
            "mean difference 0.00889/255, within the limit of under 0.5/255\n");
    const ImageDifference shifted{.largest = 1, .sum = 691'200, .count = 691'200};
    REQUIRE(describeDifference(shifted) ==
            "largest difference 1/255, within the limit of 4/255\n"
            "mean difference 1.00000/255, over the limit of under 0.5/255\n");
}

// --- the diff image (decision 231) ---------------------------------------------

TEST_CASE("the diff image is the frame, the golden and the difference times 32, side by side") {
    const ImageSize size{.width = 2, .height = 1};
    const Rgb8Image frame = imageOf(size, {10, 20, 30, 0, 0, 0});
    const GoldenImage golden{imageOf(size, {10, 21, 40, 255, 4, 8})};
    const auto diff = differenceImage(frame, golden);
    REQUIRE(diff.has_value());
    REQUIRE(diff->size().width == 6);
    REQUIRE(diff->size().height == 1);
    REQUIRE(valuesOf(*diff) == std::vector<std::uint8_t>{
                                   10,
                                   20,
                                   30,
                                   0,
                                   0,
                                   0, // the frame
                                   10,
                                   21,
                                   40,
                                   255,
                                   4,
                                   8, // the golden
                                   0,
                                   32,
                                   255,
                                   255,
                                   128,
                                   255, // 0, 1, 10 | 255, 4, 8 steps
                               });
}

TEST_CASE("the diff image keeps each row's three panels on that row") {
    // Two rows, so a panel written into the wrong row, or rows taken in the
    // wrong order, shows.
    const ImageSize size{.width = 1, .height = 2};
    const Rgb8Image frame = imageOf(size, {1, 2, 3, 4, 5, 6});
    const GoldenImage golden{imageOf(size, {1, 2, 4, 4, 5, 6})};
    const auto diff = differenceImage(frame, golden);
    REQUIRE(diff.has_value());
    REQUIRE(valuesOf(*diff) == std::vector<std::uint8_t>{
                                   1,
                                   2,
                                   3,
                                   1,
                                   2,
                                   4,
                                   0,
                                   0,
                                   32, // row 0
                                   4,
                                   5,
                                   6,
                                   4,
                                   5,
                                   6,
                                   0,
                                   0,
                                   0, // row 1
                               });
}
