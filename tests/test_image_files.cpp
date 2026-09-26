//
// Tests for view/ImageFiles.hpp: the PNG and EXR encoders (M1-16, register
// decisions 191, 196 and 197).
//
// **Each file is read back by an implementation that did not write it.**
//
//   * The PNGs are decoded by stb_image, not by lodepng, which wrote them.
//     Their IHDR chunk -- width, height, bit depth, colour type -- is also
//     read here byte by byte from the PNG specification (section 11.2.2), so
//     that the claim "8-bit RGB" or "16-bit RGB" does not rest on either
//     library.
//   * The EXR is decoded by OpenEXR's own reader. It is the library that
//     wrote it, and that is deliberate rather than an oversight: OpenEXR is
//     the reference implementation of the format, so a file it reads back is
//     an EXR by definition, and what this test adds is that *our* pixels, in
//     *our* order, are the ones in it.
//
// **The inputs are chosen so a wrong order shows.** Every channel of every
// pixel is different, a 16-bit value's two bytes differ, and the widths are
// odd, so a swapped channel, a swapped byte, a transposed row or a dropped
// column each changes the answer.
//
#include "view/ImageFiles.hpp"
#include "view/ProbeImage.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <openexr_attr.h>
#include <openexr_chunkio.h>
#include <openexr_coding.h>
#include <openexr_context.h>
#include <openexr_decode.h>
#include <openexr_errors.h>
#include <openexr_part.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace orb::view;

namespace {

constexpr ImageSize kSize{.width = 7, .height = 3};

// A distinct value for every channel of every pixel, below Limit. The limit
// is a template argument rather than a second parameter, so it cannot be
// swapped with the index (non-negotiable 1).
template <std::uint32_t Limit> [[nodiscard]] std::uint32_t sampleAt(std::size_t index) {
    return static_cast<std::uint32_t>((index * 2'654'435'761U) >> 5U) % Limit;
}

[[nodiscard]] std::vector<std::uint8_t> rgba8Pattern() {
    std::vector<std::uint8_t> values(pixelCount(kSize) * kChannelsPerPixel);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values.at(i) = static_cast<std::uint8_t>(sampleAt<256>(i));
    }
    return values;
}

[[nodiscard]] std::vector<std::uint16_t> rgba16Pattern() {
    std::vector<std::uint16_t> values(pixelCount(kSize) * kChannelsPerPixel);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values.at(i) = static_cast<std::uint16_t>(sampleAt<65'536>(i));
    }
    // Two bytes that differ, so a byte-order mistake cannot be invisible.
    values.front() = 0x0102U;
    return values;
}

// Binary16 patterns across the range, specials included: ZIP is lossless, so
// every bit must come back.
[[nodiscard]] std::vector<std::uint16_t> halfPattern() {
    std::vector<std::uint16_t> values = rgba16Pattern();
    constexpr auto kSpecial = std::to_array<std::uint16_t>({
        0x0000U,
        0x8000U,
        0x0001U,
        0x7BFFU,
        0x7C00U,
        0x7E01U,
    });
    std::ranges::copy(kSpecial, values.begin());
    return values;
}

// --- reading a PNG ------------------------------------------------------------

[[nodiscard]] std::uint32_t bigEndian32(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (const std::byte b : bytes.subspan(at, 4)) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(b);
    }
    return value;
}

// The IHDR fields, read from the specification's layout: an 8-byte signature,
// then the first chunk -- length, "IHDR", width, height, bit depth, colour type.
struct PngHeader {
    std::uint32_t width{};
    std::uint32_t height{};
    unsigned bitDepth{};
    unsigned colourType{};
};

[[nodiscard]] PngHeader pngHeaderOf(std::span<const std::byte> png) {
    constexpr std::array<std::uint8_t, 8> kSignature{{137, 80, 78, 71, 13, 10, 26, 10}};
    REQUIRE(png.size() > 33);
    for (std::size_t i = 0; i < kSignature.size(); ++i) {
        REQUIRE(std::to_integer<std::uint8_t>(png.subspan(i, 1).front()) == kSignature.at(i));
    }
    REQUIRE(bigEndian32(png, 8) == 13U);          // IHDR's data is 13 bytes
    REQUIRE(bigEndian32(png, 12) == 0x49484452U); // "IHDR"
    return {
        .width = bigEndian32(png, 16),
        .height = bigEndian32(png, 20),
        .bitDepth = std::to_integer<unsigned>(png.subspan(24, 1).front()),
        .colourType = std::to_integer<unsigned>(png.subspan(25, 1).front()),
    };
}

// stb_image's pixels, owned, freed by stb_image.
struct StbFree {
    void operator()(void* pixels) const noexcept { stbi_image_free(pixels); }
};

// What stb_image decoded: the size it read and the samples, copied out.
template <typename Sample> struct Decoded {
    int width{};
    int height{};
    int channels{};
    std::vector<Sample> samples;
};

template <typename Sample> [[nodiscard]] Decoded<Sample> decodePng(std::span<const std::byte> png) {
    Decoded<Sample> decoded;
    std::vector<stbi_uc> input(png.size());
    std::ranges::transform(
        png, input.begin(), [](std::byte b) noexcept { return std::to_integer<stbi_uc>(b); });
    const int length = static_cast<int>(input.size());
    std::unique_ptr<Sample, StbFree> pixels;
    if constexpr (sizeof(Sample) == 1) {
        pixels.reset(stbi_load_from_memory(
            input.data(), length, &decoded.width, &decoded.height, &decoded.channels, 0));
    } else {
        pixels.reset(stbi_load_16_from_memory(
            input.data(), length, &decoded.width, &decoded.height, &decoded.channels, 0));
    }
    REQUIRE(pixels != nullptr);
    const auto count = static_cast<std::size_t>(decoded.width) *
                       static_cast<std::size_t>(decoded.height) *
                       static_cast<std::size_t>(decoded.channels);
    // stb_image returns a pointer; its size is what it reported alongside. A
    // span of the two is the only way to give them bounds, which is the
    // construction -Wunsafe-buffer-usage-in-container reports (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<const Sample> view(pixels.get(), count);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    decoded.samples.assign(view.begin(), view.end());
    return decoded;
}

// The RGB of an RGBA buffer: what a PNG that drops alpha must hold.
template <typename Sample>
[[nodiscard]] std::vector<Sample> withoutAlpha(const std::vector<Sample>& rgba) {
    std::vector<Sample> rgb;
    for (std::size_t i = 0; i < rgba.size(); i += kChannelsPerPixel) {
        const auto pixel = std::span(rgba).subspan(i, 3);
        rgb.insert(rgb.end(), pixel.begin(), pixel.end());
    }
    return rgb;
}

} // namespace

TEST_CASE("an 8-bit PNG holds the RGB it was given, and no alpha") {
    const std::vector<std::uint8_t> rgba = rgba8Pattern();
    const auto png = encodePng8(kSize, rgba);
    REQUIRE(png.has_value());

    const PngHeader header = pngHeaderOf(*png);
    REQUIRE(header.width == kSize.width);
    REQUIRE(header.height == kSize.height);
    REQUIRE(header.bitDepth == 8U);
    REQUIRE(header.colourType == 2U); // truecolour: RGB, no alpha

    const Decoded<stbi_uc> decoded = decodePng<stbi_uc>(*png);
    REQUIRE(decoded.channels == 3);
    REQUIRE(decoded.samples == withoutAlpha(rgba));
}

TEST_CASE("a 16-bit PNG holds the RGB it was given, most significant byte first") {
    const std::vector<std::uint16_t> rgba = rgba16Pattern();
    const auto png = encodePng16(kSize, rgba);
    REQUIRE(png.has_value());

    const PngHeader header = pngHeaderOf(*png);
    REQUIRE(header.width == kSize.width);
    REQUIRE(header.height == kSize.height);
    REQUIRE(header.bitDepth == 16U);
    REQUIRE(header.colourType == 2U);

    const Decoded<stbi_us> decoded = decodePng<stbi_us>(*png);
    REQUIRE(decoded.channels == 3);
    REQUIRE(decoded.samples == withoutAlpha(rgba));
}

// --- reading an EXR -----------------------------------------------------------

namespace {

// A file of this test's own in the system's temporary directory, one per
// case, so that cases CTest runs in parallel never share one.
[[nodiscard]] std::filesystem::path scratchFile(std::string_view name) {
    return std::filesystem::temp_directory_path() /
           ("orbsim_test_image_files_" + std::string(name));
}

// The path as OpenEXR takes it, UTF-8 in a char string -- the conversion
// view/ImageFiles.cpp makes, written again here rather than borrowed.
[[nodiscard]] std::string utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    std::string utf8(text.size(), '\0');
    std::ranges::transform(
        text, utf8.begin(), [](char8_t unit) noexcept { return static_cast<char>(unit); });
    return utf8;
}

[[nodiscard]] std::vector<char> bytesOf(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct ExrRelease {
    void operator()(exr_context_t context) const noexcept {
        static_cast<void>(exr_finish(&context));
    }
};

[[nodiscard]] std::span<exr_coding_channel_info_t>
channelsOf(const exr_decode_pipeline_t& decoder) {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    // A pointer and a count, OpenEXR's C interface (ADR 0017).
    return {decoder.channels, static_cast<std::size_t>(decoder.channel_count)};
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

// A channel description that decodes into `plane`, rebuilt whole with the
// pointer set in its initialiser rather than assigned -- the union access
// cppcoreguidelines-pro-type-union-access reports, answered as
// view/ImageFiles.cpp answers it (register decision 204).
// The same answer to gcc's -Wmissing-braces as view/ImageFiles.cpp's (ADR 0017).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif
[[nodiscard]] exr_coding_channel_info_t writingInto(const exr_coding_channel_info_t& channel,
                                                    std::span<std::uint8_t> plane,
                                                    std::uint32_t width) {
    return {
        .channel_name = channel.channel_name,
        .height = channel.height,
        .width = channel.width,
        .x_samples = channel.x_samples,
        .y_samples = channel.y_samples,
        .p_linear = channel.p_linear,
        .bytes_per_element = channel.bytes_per_element,
        .data_type = channel.data_type,
        .user_bytes_per_element = channel.user_bytes_per_element,
        .user_data_type = channel.user_data_type,
        .user_pixel_stride = 2,
        .user_line_stride = static_cast<int32_t>(width * 2),
        // readability-trailing-comma contradicts itself on this line, the
        // designated initialiser of an anonymous union's member: with the
        // comma it reports "should not have", without it "should have"
        // (measured 2026-09-26). Off for this line alone, granted by the
        // owner (register decision 204).
        // NOLINTNEXTLINE(readability-trailing-comma)
        .decode_to_ptr = plane.data(),
    };
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// Every channel of the file, decoded to binary16, planar, by name. Each plane
// holds the bytes OpenEXR wrote, two per value in the machine's own order.
struct DecodedExr {
    std::vector<std::string> names;
    std::vector<std::vector<std::uint8_t>> planes;
};

// The binary16 bit pattern of one pixel of one plane.
[[nodiscard]] std::uint16_t halfAt(const std::vector<std::uint8_t>& plane, std::size_t pixel) {
    return std::bit_cast<std::uint16_t>(
        std::array<std::uint8_t, 2>{{plane.at(2 * pixel), plane.at((2 * pixel) + 1)}});
}

// An OpenEXR call that must succeed. One REQUIRE here rather than one per
// call keeps the functions below within the cognitive-complexity limit, which
// counts every Catch2 assertion as a branch.
void requireOk(exr_result_t result) { REQUIRE(result == EXR_ERR_SUCCESS); }

// Which chunk the decoding pipeline is being opened on: OpenEXR initialises
// it on the first and updates it on the rest. An enum rather than a bool
// (non-negotiable 2).
enum class ChunkOrder : std::uint8_t { First, Later };

void startDecoding(exr_const_context_t context,
                   const exr_chunk_info_t& chunk,
                   exr_decode_pipeline_t& decoder,
                   ChunkOrder order) {
    const exr_result_t result = order == ChunkOrder::First
                                    ? exr_decoding_initialize(context, 0, &chunk, &decoder)
                                    : exr_decoding_update(context, 0, &chunk, &decoder);
    requireOk(result);
}

// The channels' names, and a plane of the whole image for each.
void recordChannels(const exr_decode_pipeline_t& decoder, DecodedExr& decoded, ImageSize size) {
    for (const exr_coding_channel_info_t& channel : channelsOf(decoder)) {
        decoded.names.emplace_back(channel.channel_name);
        decoded.planes.emplace_back(pixelCount(size) * 2);
    }
}

// Where in the image a chunk starts: its first pixel, and the image's width.
struct ChunkPlace {
    std::size_t firstPixel{};
    std::uint32_t width{};
};

// Points every channel of the chunk at its plane, from the chunk's first pixel.
void pointChannelsInto(const exr_decode_pipeline_t& decoder,
                       DecodedExr& decoded,
                       ChunkPlace place) {
    for (auto&& [index, channel] : std::views::enumerate(channelsOf(decoder))) {
        REQUIRE(channel.data_type == EXR_PIXEL_HALF);
        const auto slot = static_cast<std::size_t>(index);
        channel = writingInto(
            channel, std::span(decoded.planes.at(slot)).subspan(place.firstPixel * 2), place.width);
    }
}

[[nodiscard]] DecodedExr decodeExr(exr_const_context_t context, ImageSize size) {
    int32_t rowsPerChunk = 0;
    requireOk(exr_get_scanlines_per_chunk(context, 0, &rowsPerChunk));
    DecodedExr decoded;
    exr_decode_pipeline_t decoder{};
    const auto destroy = [context](exr_decode_pipeline_t* pipeline) noexcept {
        static_cast<void>(exr_decoding_destroy(context, pipeline));
    };
    std::unique_ptr<exr_decode_pipeline_t, decltype(destroy)> guard{nullptr, destroy};
    const auto height = static_cast<int32_t>(size.height);
    for (int32_t row = 0; row < height; row += rowsPerChunk) {
        exr_chunk_info_t chunk{};
        requireOk(exr_read_scanline_chunk_info(context, 0, row, &chunk));
        const ChunkOrder order = guard == nullptr ? ChunkOrder::First : ChunkOrder::Later;
        startDecoding(context, chunk, decoder, order);
        if (order == ChunkOrder::First) {
            guard.reset(&decoder);
            recordChannels(decoder, decoded, size);
        }
        pointChannelsInto(
            decoder,
            decoded,
            {.firstPixel = static_cast<std::size_t>(row) * size.width, .width = size.width});
        if (order == ChunkOrder::First) {
            requireOk(exr_decoding_choose_default_routines(context, 0, &decoder));
        }
        requireOk(exr_decoding_run(context, 0, &decoder));
    }
    return decoded;
}

[[nodiscard]] std::uint32_t bitsOfFloat(float value) { return std::bit_cast<std::uint32_t>(value); }

// ZIP, and the whole image as the data window.
void requireExrLayout(exr_const_context_t context) {
    exr_compression_t compression{};
    requireOk(exr_get_compression(context, 0, &compression));
    REQUIRE(compression == EXR_COMPRESSION_ZIP);

    exr_attr_box2i_t window{};
    requireOk(exr_get_data_window(context, 0, &window));
    REQUIRE(window.min.x == 0);
    REQUIRE(window.min.y == 0);
    REQUIRE(window.max.x == static_cast<int32_t>(kSize.width) - 1);
    REQUIRE(window.max.y == static_cast<int32_t>(kSize.height) - 1);
}

// The Rec. 709 primaries and D65 white, each coordinate bit for bit.
void requireExrColours(exr_const_context_t context) {
    exr_attr_chromaticities_t chroma{};
    requireOk(exr_attr_get_chromaticities(context, 0, "chromaticities", &chroma));
    const auto got = std::to_array<std::uint32_t>({
        bitsOfFloat(chroma.red_x),
        bitsOfFloat(chroma.red_y),
        bitsOfFloat(chroma.green_x),
        bitsOfFloat(chroma.green_y),
        bitsOfFloat(chroma.blue_x),
        bitsOfFloat(chroma.blue_y),
        bitsOfFloat(chroma.white_x),
        bitsOfFloat(chroma.white_y),
    });
    const auto want = std::to_array<std::uint32_t>({
        bitsOfFloat(0.640F),
        bitsOfFloat(0.330F),
        bitsOfFloat(0.300F),
        bitsOfFloat(0.600F),
        bitsOfFloat(0.150F),
        bitsOfFloat(0.060F),
        bitsOfFloat(0.3127F),
        bitsOfFloat(0.3290F),
    });
    REQUIRE(got == want);
}

void requireExrComment(exr_const_context_t context) {
    int32_t commentLength = 0;
    const char* comment = nullptr;
    requireOk(exr_attr_get_string(context, 0, "comments", &commentLength, &comment));
    REQUIRE(std::string_view(comment) == "radiance, W/(m^2 sr)");
}

// Every value of every channel, bit for bit, against the RGBA it came from.
void requireExrPixels(const DecodedExr& decoded, const std::vector<std::uint16_t>& rgbaHalf) {
    // OpenEXR stores channels sorted by name; there is no alpha.
    REQUIRE(decoded.names == std::vector<std::string>{"B", "G", "R"});
    constexpr std::array<std::size_t, 3> kOffsetInRgba{{2, 1, 0}}; // B, G, R
    for (std::size_t c = 0; c < kOffsetInRgba.size(); ++c) {
        for (std::size_t pixel = 0; pixel < pixelCount(kSize); ++pixel) {
            INFO("channel " << decoded.names.at(c) << ", pixel " << pixel);
            REQUIRE(halfAt(decoded.planes.at(c), pixel) ==
                    rgbaHalf.at((pixel * kChannelsPerPixel) + kOffsetInRgba.at(c)));
        }
    }
}

} // namespace

TEST_CASE("an EXR holds the HDR target's bits, as R, G and B, with its colours and comment") {
    const std::vector<std::uint16_t> rgbaHalf = halfPattern();
    const std::filesystem::path path = scratchFile("contents.exr");
    const auto written = writeExr(path, kSize, rgbaHalf, "radiance, W/(m^2 sr)");
    INFO((written.has_value() ? std::string("written") : written.error().message));
    REQUIRE(written.has_value());

    exr_context_t started = nullptr;
    const std::string name = utf8Of(path);
    requireOk(exr_start_read(&started, name.c_str(), nullptr));
    const std::unique_ptr<std::remove_pointer_t<exr_context_t>, ExrRelease> context{started};

    requireExrLayout(context.get());
    requireExrColours(context.get());
    requireExrComment(context.get());
    requireExrPixels(decodeExr(context.get(), kSize), rgbaHalf);
}

TEST_CASE("each encoder writes the same bytes twice") {
    // Determinism is a tested property (VERIFICATION.md rule 16): a probe's
    // files depend on the frame alone.
    const auto png8 = encodePng8(kSize, rgba8Pattern());
    const auto png16 = encodePng16(kSize, rgba16Pattern());
    REQUIRE(png8.has_value());
    REQUIRE(png16.has_value());
    REQUIRE(*png8 == encodePng8(kSize, rgba8Pattern()).value());
    REQUIRE(*png16 == encodePng16(kSize, rgba16Pattern()).value());

    const std::filesystem::path first = scratchFile("twice-1.exr");
    const std::filesystem::path second = scratchFile("twice-2.exr");
    REQUIRE(writeExr(first, kSize, halfPattern(), "x").has_value());
    REQUIRE(writeExr(second, kSize, halfPattern(), "x").has_value());
    REQUIRE(bytesOf(first) == bytesOf(second));
}
