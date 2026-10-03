#include "view/ImageCompare.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "view/ProbeImage.hpp"
#include "view/Srgb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orb::view {
namespace {

// The largest 8-bit value, and the number of steps from black to white.
constexpr std::uint32_t kFullScale = 255;
constexpr f64 kSteps = 255.0;

// The straight segment, checked against the standard's knees rather than
// claimed: 10/255 decodes on the linear branch and 11/255 does not, and the
// largest value there decodes below the encode's own knee, so the average of
// four such values is encoded on the linear branch too -- which is what makes
// the linear-light average exactly the mean of the four.
static_assert(static_cast<f64>(kLastStraightValue) / kSteps <= kSrgbEncodedKnee);
static_assert(static_cast<f64>(kLastStraightValue + 1) / kSteps > kSrgbEncodedKnee);
static_assert(static_cast<f64>(kLastStraightValue) / kSteps / kSrgbLinearSlope <= kSrgbLinearKnee);

// An 8-bit value's light: k stands for the encoded value k/255, exactly.
[[nodiscard]] f64 linearOf(std::uint8_t value) {
    return srgbDecode(EncodedValue{static_cast<f64>(value) / kSteps}).value();
}

// One value of the frame and the same value of the golden, by name, so that
// the two cannot be handed over the wrong way round (non-negotiable 1).
struct ValuePair {
    std::uint8_t frame{};
    std::uint8_t golden{};
};

[[nodiscard]] std::uint32_t absoluteDifference(ValuePair pair) noexcept {
    return pair.frame > pair.golden ? static_cast<std::uint32_t>(pair.frame - pair.golden)
                                    : static_cast<std::uint32_t>(pair.golden - pair.frame);
}

[[nodiscard]] bool sameSize(const Rgb8Image& frame, const GoldenImage& golden) noexcept {
    return frame.size().width == golden.size().width && frame.size().height == golden.size().height;
}

// Where a value lives in an image: pixel (column, row), channel 0 to 2.
struct ValuePlace {
    std::size_t column{};
    std::size_t row{};
    std::size_t channel{};
};

[[nodiscard]] std::size_t indexOf(std::uint32_t width, ValuePlace place) noexcept {
    return (((place.row * width) + place.column) * kRgbChannels) + place.channel;
}

} // namespace

Rgb8Image::Rgb8Image(ImageSize size, std::vector<std::uint8_t> rgb) noexcept
    : size_(size), rgb_(std::move(rgb)) {}

std::expected<Rgb8Image, ImageCompareError> Rgb8Image::from(ImageSize size,
                                                            std::vector<std::uint8_t> rgb) {
    if (size.width == 0 || size.height == 0) {
        return std::unexpected(ImageCompareError::EmptyImage);
    }
    if (rgb.size() != pixelCount(size) * kRgbChannels) {
        return std::unexpected(ImageCompareError::WrongValueCount);
    }
    return Rgb8Image{size, std::move(rgb)};
}

Rgb8Image Rgb8Image::fromRgba(ImageSize size, std::span<const std::uint8_t> rgba) {
    ORBSIM_EXPECTS(size.width > 0 && size.height > 0);
    ORBSIM_EXPECTS(rgba.size() == pixelCount(size) * kChannelsPerPixel);
    std::vector<std::uint8_t> rgb;
    rgb.reserve(pixelCount(size) * kRgbChannels);
    for (std::size_t i = 0; i < rgba.size(); i += kChannelsPerPixel) {
        const auto pixel = rgba.subspan(i, kRgbChannels);
        rgb.insert(rgb.end(), pixel.begin(), pixel.end());
    }
    return Rgb8Image{size, std::move(rgb)};
}

Rgb16Image::Rgb16Image(ImageSize size, std::vector<std::uint16_t> rgb) noexcept
    : size_(size), rgb_(std::move(rgb)) {}

std::expected<Rgb16Image, ImageCompareError> Rgb16Image::from(ImageSize size,
                                                              std::vector<std::uint16_t> rgb) {
    if (size.width == 0 || size.height == 0) {
        return std::unexpected(ImageCompareError::EmptyImage);
    }
    if (rgb.size() != pixelCount(size) * kRgbChannels) {
        return std::unexpected(ImageCompareError::WrongValueCount);
    }
    return Rgb16Image{size, std::move(rgb)};
}

GoldenImage::GoldenImage(Rgb8Image image) noexcept : image_(std::move(image)) {}

std::uint8_t averageInLinearLight(std::array<std::uint8_t, 4> block) {
    const bool straight =
        std::ranges::all_of(block, [](std::uint8_t v) noexcept { return v <= kLastStraightValue; });
    if (straight) {
        // The mean of four whole numbers, rounded to nearest with a tie
        // going up -- exact, where floating point would decide a tie by the
        // rounding of 1/12.92 (register decision 228).
        std::uint32_t sum = 0;
        for (const std::uint8_t v : block) {
            sum += v;
        }
        return static_cast<std::uint8_t>((sum + 2U) / 4U);
    }
    // Summed in a fixed order, so that one build always gives one answer.
    f64 light = 0.0;
    for (const std::uint8_t v : block) {
        light += linearOf(v);
    }
    const f64 encoded = srgbEncode(LinearValue{light / 4.0}).value();
    // std::round rather than floor(x + 0.5), which rounds 0.49999999999999994
    // up. The encode clamps to [0, 1], so the result is within 0 to 255.
    const f64 steps = std::round(encoded * kSteps);
    ORBSIM_ENSURES(steps >= 0.0 && steps <= kSteps);
    return static_cast<std::uint8_t>(steps);
}

std::expected<Rgb8Image, ImageCompareError> halveInLinearLight(const Rgb8Image& image) {
    const ImageSize size = image.size();
    if (size.width % 2 != 0 || size.height % 2 != 0) {
        return std::unexpected(ImageCompareError::OddDimensions);
    }
    const ImageSize half{.width = size.width / 2, .height = size.height / 2};
    const auto at = [&](ValuePlace place) { return image.value(indexOf(size.width, place)); };
    std::vector<std::uint8_t> rgb(pixelCount(half) * kRgbChannels);
    for (std::size_t row = 0; row < half.height; ++row) {
        for (std::size_t column = 0; column < half.width; ++column) {
            const std::size_t left = 2 * column;
            const std::size_t top = 2 * row;
            for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
                rgb.at(indexOf(half.width, {.column = column, .row = row, .channel = channel})) =
                    averageInLinearLight(std::to_array<std::uint8_t>({
                        at({.column = left, .row = top, .channel = channel}),
                        at({.column = left + 1, .row = top, .channel = channel}),
                        at({.column = left, .row = top + 1, .channel = channel}),
                        at({.column = left + 1, .row = top + 1, .channel = channel}),
                    }));
            }
        }
    }
    return Rgb8Image::from(half, std::move(rgb));
}

std::expected<ImageDifference, ImageCompareError> measureDifference(const Rgb8Image& frame,
                                                                    const GoldenImage& golden) {
    if (!sameSize(frame, golden)) {
        return std::unexpected(ImageCompareError::DimensionsDiffer);
    }
    ImageDifference difference{.largest = 0, .sum = 0, .count = frame.valueCount()};
    for (std::size_t i = 0; i < frame.valueCount(); ++i) {
        const std::uint32_t d =
            absoluteDifference({.frame = frame.value(i), .golden = golden.value(i)});
        difference.largest = std::max(difference.largest, d);
        difference.sum += d;
    }
    return difference;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
std::string formatLargest(const ImageDifference& difference) {
    return std::format("{}/255", difference.largest);
}

std::string formatMean(const ImageDifference& difference) {
    // For reading only: the verdict is taken in whole numbers.
    const f64 mean = difference.count == 0
                         ? 0.0
                         : static_cast<f64>(difference.sum) / static_cast<f64>(difference.count);
    return std::format("{:.5f}/255", mean);
}

std::string describeDifference(const ImageDifference& difference) {
    const f64 meanLimit =
        static_cast<f64>(kGoldenMeanNumerator) / static_cast<f64>(kGoldenMeanDenominator);
    const std::string_view largestVerdict = largestWithinTolerance(difference) ? "within" : "over";
    const std::string_view meanVerdict = meanWithinTolerance(difference) ? "within" : "over";
    return std::format("largest difference {}, {} the limit of {}/255\n"
                       "mean difference {}, {} the limit of under {}/255\n",
                       formatLargest(difference),
                       largestVerdict,
                       kGoldenLargestSteps,
                       formatMean(difference),
                       meanVerdict,
                       meanLimit);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

std::expected<Rgb8Image, ImageCompareError> differenceImage(const Rgb8Image& frame,
                                                            const GoldenImage& golden) {
    if (!sameSize(frame, golden)) {
        return std::unexpected(ImageCompareError::DimensionsDiffer);
    }
    const ImageSize size = frame.size();
    const ImageSize wide{.width = size.width * 3, .height = size.height};
    std::vector<std::uint8_t> rgb(pixelCount(wide) * kRgbChannels);
    for (std::size_t row = 0; row < size.height; ++row) {
        for (std::size_t column = 0; column < size.width; ++column) {
            for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
                const std::size_t from =
                    indexOf(size.width, {.column = column, .row = row, .channel = channel});
                const auto panel = [&](std::size_t which) {
                    return indexOf(
                        wide.width,
                        {.column = (which * size.width) + column, .row = row, .channel = channel});
                };
                const ValuePair pair{.frame = frame.value(from), .golden = golden.value(from)};
                rgb.at(panel(0)) = pair.frame;
                rgb.at(panel(1)) = pair.golden;
                rgb.at(panel(2)) = static_cast<std::uint8_t>(
                    std::min(absoluteDifference(pair) * kDiffAmplification, kFullScale));
            }
        }
    }
    return Rgb8Image::from(wide, std::move(rgb));
}

} // namespace orb::view
