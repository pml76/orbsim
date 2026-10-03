//
// Writes altered copies of a probe's own frame, halved as a golden is, for the
// tests that show a mismatch is caught end to end (M1-17; ADR 0008; register
// decision 236).
//
//     make_broken_goldens <frame.png> <output directory>
//
// A golden cannot be broken by a switch in the application -- that would put
// test code in the product (register decision 193) -- so the tests hand the
// probe a golden that is wrong instead.
//
// **Made from the run's own frame since M1-110** (register decisions 290 and
// 296): the 1280x720 PNG probe_clear has just written, halved to 640x360 by
// the comparison's own halveInLinearLight. The probe renders the same frame
// again and halves it the same way, so the alteration is the whole of the
// difference on every graphics card, no approved golden is needed, and the
// tests can pin the exact numbers. Until M1-110 the copies were made from
// the committed golden, which on any card but the one it was approved on
// added that card's own distance from it (decision 275).
//
// Three copies, each wrong in one way:
//
//   * <name>-block.png: a 16x16 block in the middle moved by 8 steps in every
//     channel. Only the cap catches it -- 8 is over 4, and the mean is 6,144
//     over 691,200 values, 0.0089 of a step, far under half of one.
//   * <name>-shift.png: every value moved by 1 step. Only the mean catches
//     it -- 1 is under the cap of 4, and the mean is exactly 1, over 0.5.
//   * <name>-wrong-size.png: the top-left 320x180 of it, which cannot be
//     compared at all and must be reported by both sizes.
//
// A value that cannot move up moves down instead, by the same amount, so the
// size of every change is exactly what is stated above.
//
// Built from orbsim_view's own reader and writer, which test_image_files holds
// to stb_image and to the PNG specification. The probe tests that use these
// files assert the exit code and the numbers printed; this program asserts
// nothing, and fails only if it cannot read or write.
//
#include "view/ImageCompare.hpp"
#include "view/ImageFiles.hpp"
#include "view/ProbeImage.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace orb::view;

namespace {

// The block: 16x16, centred on the 640x360 golden.
constexpr std::size_t kBlockSide = 16;
constexpr std::uint8_t kBlockSteps = 8;
constexpr std::uint8_t kShiftSteps = 1;
constexpr ImageSize kWrongSize{.width = 320, .height = 180};

[[nodiscard]] std::expected<std::vector<std::byte>, std::string>
readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected("cannot open " + path.string());
    const std::vector<char> chars{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

[[nodiscard]] std::expected<void, std::string> writeImage(const std::filesystem::path& path,
                                                          const Rgb8Image& image) {
    const auto png = encodePng8(image);
    if (!png) return std::unexpected(png.error().message);
    std::vector<char> chars(png->size());
    std::ranges::transform(*png, chars.begin(), [](std::byte b) { return static_cast<char>(b); });
    std::ofstream file(path, std::ios::binary);
    if (!file) return std::unexpected("cannot open " + path.string() + " for writing");
    file.write(chars.data(), static_cast<std::streamsize>(chars.size()));
    if (!file) return std::unexpected("could not write all of " + path.string());
    return {};
}

[[nodiscard]] std::vector<std::uint8_t> valuesOf(const Rgb8Image& image) {
    std::vector<std::uint8_t> values(image.valueCount());
    for (std::size_t i = 0; i < values.size(); ++i) {
        values.at(i) = image.value(i);
    }
    return values;
}

// `value` moved by Steps, up if it can go that far and down if not. The
// amount is a template argument rather than a second parameter, so it cannot
// be swapped with the value (non-negotiable 1).
template <std::uint8_t Steps> [[nodiscard]] std::uint8_t moved(std::uint8_t value) {
    return value + Steps <= 255 ? static_cast<std::uint8_t>(value + Steps)
                                : static_cast<std::uint8_t>(value - Steps);
}

[[nodiscard]] std::expected<Rgb8Image, ImageCompareError> withBlock(const Rgb8Image& golden) {
    const ImageSize size = golden.size();
    std::vector<std::uint8_t> values = valuesOf(golden);
    const std::size_t left = (size.width - kBlockSide) / 2;
    const std::size_t top = (size.height - kBlockSide) / 2;
    for (std::size_t row = top; row < top + kBlockSide; ++row) {
        for (std::size_t column = left; column < left + kBlockSide; ++column) {
            for (std::size_t channel = 0; channel < kRgbChannels; ++channel) {
                std::uint8_t& v =
                    values.at((((row * size.width) + column) * kRgbChannels) + channel);
                v = moved<kBlockSteps>(v);
            }
        }
    }
    return Rgb8Image::from(size, std::move(values));
}

[[nodiscard]] std::expected<Rgb8Image, ImageCompareError> shifted(const Rgb8Image& golden) {
    std::vector<std::uint8_t> values = valuesOf(golden);
    std::ranges::transform(
        values, values.begin(), [](std::uint8_t v) { return moved<kShiftSteps>(v); });
    return Rgb8Image::from(golden.size(), std::move(values));
}

[[nodiscard]] std::expected<Rgb8Image, ImageCompareError> cropped(const Rgb8Image& golden) {
    std::vector<std::uint8_t> values;
    values.reserve(pixelCount(kWrongSize) * kRgbChannels);
    for (std::size_t row = 0; row < kWrongSize.height; ++row) {
        for (std::size_t i = 0; i < kWrongSize.width * kRgbChannels; ++i) {
            values.push_back(golden.value((row * golden.size().width * kRgbChannels) + i));
        }
    }
    return Rgb8Image::from(kWrongSize, std::move(values));
}

// Where the frame is and where the copies go, by name (non-negotiable 1).
struct Paths {
    std::filesystem::path frame;
    std::filesystem::path outDirectory;
};

// One altered copy: its file name and its image, or why it could not be made.
struct Copy {
    std::string name;
    std::expected<Rgb8Image, ImageCompareError> image;
};

[[nodiscard]] int make(const Paths& paths) {
    const auto bytes = readFile(paths.frame);
    if (!bytes) {
        std::print(stderr, "make_broken_goldens: {}\n", bytes.error());
        return 1;
    }
    const auto frame = decodePng8(*bytes);
    if (!frame) {
        std::print(stderr,
                   "make_broken_goldens: {}: {} ({})\n",
                   paths.frame.string(),
                   describe(frame.error().error),
                   frame.error().detail);
        return 1;
    }
    // Halved exactly as the probe halves the frame it compares (decision 290).
    const auto golden = halveInLinearLight(*frame);
    if (!golden) {
        std::print(stderr,
                   "make_broken_goldens: {}: {}\n",
                   paths.frame.string(),
                   describe(golden.error()));
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(paths.outDirectory, error);
    if (error) {
        std::print(stderr,
                   "make_broken_goldens: cannot create {}: {}\n",
                   paths.outDirectory.string(),
                   error.message());
        return 1;
    }
    const std::string stem = paths.frame.stem().string();
    const std::vector<Copy> copies{
        {.name = stem + "-block.png", .image = withBlock(*golden)},
        {.name = stem + "-shift.png", .image = shifted(*golden)},
        {.name = stem + "-wrong-size.png", .image = cropped(*golden)},
    };
    for (const Copy& copy : copies) {
        if (!copy.image) {
            std::print(
                stderr, "make_broken_goldens: {}: {}\n", copy.name, describe(copy.image.error()));
            return 1;
        }
        if (auto ok = writeImage(paths.outDirectory / copy.name, *copy.image); !ok) {
            std::print(stderr, "make_broken_goldens: {}\n", ok.error());
            return 1;
        }
        std::print("wrote {}\n", (paths.outDirectory / copy.name).string());
    }
    return 0;
}

} // namespace

// main is the one function nothing may escape from, so its last-resort
// handler uses std::fputs, which cannot throw, rather than std::print --
// src/app/main.cpp's arrangement, and for its reasons. The arguments and
// std::fputs are the C runtime's interface: a pointer and a count, whose span
// is the construction -Wunsafe-buffer-usage-in-container reports, and a C
// function taking an unbounded string, which -Wunsafe-buffer-usage-in-libc-call
// reports; both are off for this function alone (ADR 0017). The arguments are
// copied into a vector at once, so they are read through a checked at().
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
int main(int argc, char** argv) {
    try {
        const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
        const std::vector<std::string_view> tokens(args.begin(), args.end());
        if (tokens.size() != 3) {
            std::print(stderr, "usage: make_broken_goldens <frame.png> <output directory>\n");
            return 2;
        }
        return make({.frame = tokens.at(1), .outDirectory = tokens.at(2)});
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("make_broken_goldens: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("make_broken_goldens: unhandled exception\n", stderr));
        return 1;
    }
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
