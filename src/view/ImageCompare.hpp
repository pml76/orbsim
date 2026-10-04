#ifndef ORBSIM_VIEW_IMAGECOMPARE_HPP
#define ORBSIM_VIEW_IMAGECOMPARE_HPP
//
// Comparing a probe's frame with its golden image (M1-17; ADR 0008; register
// decisions 228-231).
//
// **A golden image is a frame the owner has approved.** A probe's 1280x720
// display image is shrunk to 640x360 and held against the committed golden by
// two tolerances that fail differently: no value further than 4/255 from the
// golden, which catches a small wrong region, and a mean difference under
// 0.5/255, which catches a shift over the whole image that stays under the
// first. On a mismatch a diff image shows the two side by side with their
// difference, so the failure can be read from the files alone.
//
// **Everything here is headless and pure**, which is why it is in orbsim_view:
// tests/test_image_compare.cpp holds it to hand-computed and 50-digit values
// without a GPU (ADR 0012).
//
// **Every value is an 8-bit step, 1/255 of full scale**, as a PNG stores it,
// and the tolerances are counted in the same steps. The comparison is in whole
// numbers throughout, so no rounding enters a verdict (decision 229).
//
#include "view/ProbeImage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orb::view {

// The ways an image can be refused or two images fail to be comparable.
// Reported rather than asserted: a golden is a file, and a file can be any
// size (ADR 0002). Each name says what is wrong with the images.
enum class ImageCompareError : std::uint8_t {
    EmptyImage,       // a width or a height of zero
    WrongValueCount,  // the values do not fill width x height x 3
    OddDimensions,    // a 2x2 downsample needs an even width and height
    DimensionsDiffer, // the two images are not the same size
};

[[nodiscard]] constexpr std::string_view describe(ImageCompareError error) noexcept {
    switch (error) {
    case ImageCompareError::EmptyImage:
        return "the image has no pixels: its width or its height is zero";
    case ImageCompareError::WrongValueCount:
        return "the image's values do not fill its width times its height times three channels";
    case ImageCompareError::OddDimensions:
        return "a 2x2 downsample needs an even width and an even height";
    case ImageCompareError::DimensionsDiffer:
        return "the two images are not the same size";
    }
    return "unknown image comparison error";
}

static_assert(describe(ImageCompareError::EmptyImage) !=
              describe(ImageCompareError::WrongValueCount));
static_assert(describe(ImageCompareError::WrongValueCount) !=
              describe(ImageCompareError::OddDimensions));
static_assert(describe(ImageCompareError::OddDimensions) !=
                  describe(ImageCompareError::DimensionsDiffer),
              "each refusal says something different");

// Three channels -- R, G, B -- in an image that is compared: a PNG golden has
// no alpha, and every probe frame is opaque.
inline constexpr std::size_t kRgbChannels = 3;

// An 8-bit RGB image, rows top to bottom, each pixel R, G, B.
//
// **Validated at construction** (ADR 0022): an image whose values do not fill
// its size cannot be built, so nothing that takes one has to check.
class Rgb8Image {
public:
    [[nodiscard]] static std::expected<Rgb8Image, ImageCompareError>
    from(ImageSize size, std::vector<std::uint8_t> rgb);

    // The RGB of a frame read back as RGBA, alpha dropped. `rgba` must hold
    // pixelCount(size) * 4 values; that is asserted, as encodePng8 asserts
    // it, because the readback that produces it sizes it from the same size.
    [[nodiscard]] static Rgb8Image fromRgba(ImageSize size, std::span<const std::uint8_t> rgba);

    [[nodiscard]] ImageSize size() const noexcept { return size_; }

    // How many values the image holds, width x height x 3, and one of them,
    // row by row and R, G, B within a pixel. **By value, not a view of the
    // storage**: a view would want [[clang::lifetimebound]], whose portable
    // spelling lived in render/VulkanHandle.hpp, and view/Mat4.hpp and
    // view/VertexLayout.hpp give way to that the same way. *(The macro is
    // core/Attributes.hpp's since M1-19, register decision 312, so a view is
    // possible now; changing this was not that task's.)*
    [[nodiscard]] std::size_t valueCount() const noexcept { return rgb_.size(); }
    [[nodiscard]] std::uint8_t value(std::size_t index) const { return rgb_.at(index); }

private:
    Rgb8Image(ImageSize size, std::vector<std::uint8_t> rgb) noexcept;

    ImageSize size_;
    std::vector<std::uint8_t> rgb_;
};

// A 16-bit RGB image, rows top to bottom, each pixel R, G, B: a probe's
// 16-bit display image read back for the port check (M1-18, register decision
// 260). Validated at construction, as Rgb8Image is.
class Rgb16Image {
public:
    [[nodiscard]] static std::expected<Rgb16Image, ImageCompareError>
    from(ImageSize size, std::vector<std::uint16_t> rgb);

    [[nodiscard]] ImageSize size() const noexcept { return size_; }
    // By value, for the reason Rgb8Image gives.
    [[nodiscard]] std::size_t valueCount() const noexcept { return rgb_.size(); }
    [[nodiscard]] std::uint16_t value(std::size_t index) const { return rgb_.at(index); }

private:
    Rgb16Image(ImageSize size, std::vector<std::uint16_t> rgb) noexcept;

    ImageSize size_;
    std::vector<std::uint16_t> rgb_;
};

// A golden image, as read from its file. **A type of its own** so that the
// frame and the golden cannot be handed to a comparison the wrong way round
// (non-negotiable 1): the diff image puts them in named places.
class GoldenImage {
public:
    explicit GoldenImage(Rgb8Image image) noexcept;
    [[nodiscard]] ImageSize size() const noexcept { return image_.size(); }
    [[nodiscard]] std::size_t valueCount() const noexcept { return image_.valueCount(); }
    [[nodiscard]] std::uint8_t value(std::size_t index) const { return image_.value(index); }

private:
    Rgb8Image image_;
};

// --- the downsample (register decision 228) ----------------------------------

// The largest 8-bit value on the straight segment of the sRGB curve: 10/255 is
// below the decode's knee at 0.04045 and 11/255 above it (IEC 61966-2-1).
// Four values up to this one average to a value that is also there, and there
// the linear-light average is exactly the mean of the four.
inline constexpr std::uint8_t kLastStraightValue = 10;

// One 2x2 block's four values of one channel, averaged in the display's
// linear light and rounded to the nearest 8-bit value: decoded with the sRGB
// standard, averaged, encoded again. **Where all four are on the straight
// segment the result is computed in whole numbers**, (a + b + c + d + 2) / 4,
// because exact ties occur there and floating point would decide them by
// rounding noise; an exact tie rounds up.
[[nodiscard]] std::uint8_t averageInLinearLight(std::array<std::uint8_t, 4> block);

// The image at half its width and height, each channel of each 2x2 block
// averaged by averageInLinearLight.
[[nodiscard]] std::expected<Rgb8Image, ImageCompareError>
halveInLinearLight(const Rgb8Image& image);

// --- the two measurements and their tolerances (register decision 229) --------

// How far a frame is from its golden, over every R, G and B value.
struct ImageDifference {
    std::uint32_t largest{}; // the largest absolute difference, in 8-bit steps
    std::uint64_t sum{};     // the sum of every absolute difference, in 8-bit steps
    std::uint64_t count{};   // how many values were compared
};

[[nodiscard]] std::expected<ImageDifference, ImageCompareError>
measureDifference(const Rgb8Image& frame, const GoldenImage& golden);

// No value further than 4 steps from the golden; 5 fails (ADR 0008).
inline constexpr std::uint32_t kGoldenLargestSteps = 4;

// The mean difference must be under half a step (ADR 0008), written as the
// fraction it is so that the test is exact: sum / count < 1 / 2.
inline constexpr std::uint64_t kGoldenMeanNumerator = 1;
inline constexpr std::uint64_t kGoldenMeanDenominator = 2;

[[nodiscard]] constexpr bool largestWithinTolerance(const ImageDifference& difference) noexcept {
    return difference.largest <= kGoldenLargestSteps;
}

[[nodiscard]] constexpr bool meanWithinTolerance(const ImageDifference& difference) noexcept {
    return difference.sum * kGoldenMeanDenominator < difference.count * kGoldenMeanNumerator;
}

[[nodiscard]] constexpr bool matchesGolden(const ImageDifference& difference) noexcept {
    return largestWithinTolerance(difference) && meanWithinTolerance(difference);
}

// The two measurements, each beside its limit and whether it is within it,
// for a person to read: two lines, each ending in a newline. The mean is
// printed to five decimals for reading only; the verdict is
// meanWithinTolerance, in whole numbers.
// gcc's -Wabi-tag: std::string carries libstdc++'s "cxx11" ABI tag, and
// gcc wants everything holding or returning one to carry it too. The tag
// guards code shipped as a binary against the old string ABI; this project
// builds everything from source with one ABI. Off at this site alone, for
// gcc alone -- register decision 52's ruling and shape.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
[[nodiscard]] std::string describeDifference(const ImageDifference& difference);
// Each measurement alone, "8/255" and "0.00889/255", for the sidecar.
[[nodiscard]] std::string formatLargest(const ImageDifference& difference);
[[nodiscard]] std::string formatMean(const ImageDifference& difference);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// --- the diff image (register decision 231) ----------------------------------

// How much the difference panel magnifies: a difference of one step shows as
// 32, the 4-step cap as 128, and 8 steps or more as full scale.
inline constexpr std::uint32_t kDiffAmplification = 32;

// Three times the width: the frame, the golden, and each channel's absolute
// difference times kDiffAmplification, capped at 255, side by side.
[[nodiscard]] std::expected<Rgb8Image, ImageCompareError>
differenceImage(const Rgb8Image& frame, const GoldenImage& golden);

} // namespace orb::view

#endif // ORBSIM_VIEW_IMAGECOMPARE_HPP
