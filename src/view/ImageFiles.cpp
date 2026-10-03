#include "view/ImageFiles.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "view/ImageCompare.hpp"
#include "view/ProbeImage.hpp"

#include <lodepng.h>
#include <openexr_attr.h>
#include <openexr_chunkio.h>
#include <openexr_coding.h>
#include <openexr_context.h>
#include <openexr_encode.h>
#include <openexr_errors.h>
#include <openexr_part.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace orb::view {
namespace {

[[nodiscard]] std::unexpected<ImageFileError> fail(std::string message) {
    return std::unexpected(ImageFileError{.message = std::move(message)});
}

// lodepng's result code, as its own text: 0 is success, anything else a
// failure it can name.
[[nodiscard]] std::expected<std::vector<std::byte>, ImageFileError>
encodePngRgb(ImageSize size, const std::vector<unsigned char>& rgb, unsigned bitDepth) {
    // RGB at the stated depth, always: lodepng otherwise chooses the smallest
    // colour type the pixels allow -- a palette, grey, even 1 bit -- which is
    // not the 8- or 16-bit RGB decision 191 says a probe's PNG is, and not a
    // golden decision 235 can read. Found by M1-17 on 2026-09-29, when a
    // 320x180 crop of `clear` came out as a palette.
    lodepng::State state;
    state.info_raw.colortype = LCT_RGB;
    state.info_raw.bitdepth = bitDepth;
    state.info_png.color.colortype = LCT_RGB;
    state.info_png.color.bitdepth = bitDepth;
    state.encoder.auto_convert = 0;
    std::vector<unsigned char> png;
    const unsigned error = lodepng::encode(png, rgb, size.width, size.height, state);
    if (error != 0) return fail(std::string("PNG encoding failed: ") + lodepng_error_text(error));
    std::vector<std::byte> bytes(png.size());
    std::ranges::transform(
        png, bytes.begin(), [](unsigned char c) noexcept { return std::byte{c}; });
    return bytes;
}

// --- reading a golden (M1-17) ------------------------------------------------

[[nodiscard]] std::unexpected<PngReadFailure> refuse(PngReadError error, std::string detail) {
    return std::unexpected(PngReadFailure{.error = error, .detail = std::move(detail)});
}

// The PNG specification's layout (sections 5.2 and 11.2.2): an 8-byte
// signature, then the IHDR chunk -- its length (13), its type, then width,
// height, bit depth and colour type -- and its CRC, 33 bytes in all.
constexpr std::array<std::uint8_t, 8> kPngSignature{{137, 80, 78, 71, 13, 10, 26, 10}};
constexpr std::size_t kIhdrLengthAt = 8;
constexpr std::size_t kIhdrTypeAt = 12;
constexpr std::size_t kIhdrWidthAt = 16;
constexpr std::size_t kIhdrHeightAt = 20;
constexpr std::size_t kIhdrBitDepthAt = 24;
constexpr std::size_t kIhdrColourTypeAt = 25;
constexpr std::size_t kIhdrEnd = 33;
constexpr std::uint32_t kIhdrLength = 13;
constexpr std::uint32_t kIhdrType = 0x49484452U; // "IHDR"
constexpr unsigned kPngColourTypeRgb = 2;        // truecolour, no alpha
constexpr int kRgbChannelsAsInt = 3;             // stb_image's desired channel count

[[nodiscard]] std::uint32_t bigEndian32(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (const std::byte b : bytes.subspan(at, 4)) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(b);
    }
    return value;
}

[[nodiscard]] bool hasPngHeader(std::span<const std::byte> png) {
    if (png.size() < kIhdrEnd) return false;
    const bool signature = std::ranges::equal(
        png.first(kPngSignature.size()), kPngSignature, [](std::byte b, std::uint8_t s) noexcept {
            return std::to_integer<std::uint8_t>(b) == s;
        });
    return signature && bigEndian32(png, kIhdrLengthAt) == kIhdrLength &&
           bigEndian32(png, kIhdrTypeAt) == kIhdrType;
}

// stb_image's pixels, owned, freed by stb_image.
struct StbFree {
    void operator()(stbi_uc* pixels) const noexcept { stbi_image_free(pixels); }
};
struct StbFree16 {
    void operator()(stbi_us* pixels) const noexcept { stbi_image_free(pixels); }
};

// --- EXR ---------------------------------------------------------------------

// The path as OpenEXR takes it: UTF-8 in a `char` string, which it widens on
// Windows. Relabelling char8_t as char is the whole conversion; the bytes are
// already UTF-8.
[[nodiscard]] std::string utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    std::string utf8(text.size(), '\0');
    std::ranges::transform(
        text, utf8.begin(), [](char8_t unit) noexcept { return static_cast<char>(unit); });
    return utf8;
}

// OpenEXR's error callback carries no user pointer of its own; the context's
// does, and it is the string the message is kept in.
void onExrError(exr_const_context_t context, exr_result_t /*code*/, const char* message) {
    void* user = nullptr;
    if (exr_get_user_data(context, &user) == EXR_ERR_SUCCESS && user != nullptr) {
        *static_cast<std::string*>(user) = message != nullptr ? message : "";
    }
}

// An OpenEXR call's result, as ours, with the library's own message.
[[nodiscard]] std::expected<void, ImageFileError>
exrCheck(exr_result_t result, std::string_view what, const std::string& lastError) {
    if (result == EXR_ERR_SUCCESS) return {};
    std::string message =
        "EXR writing failed: " + std::string(what) + ": " + exr_get_error_code_as_string(result);
    if (!lastError.empty()) message += " (" + lastError + ")";
    return fail(std::move(message));
}

// The chromaticities of the Rec. 709 primaries and D65 white -- the primaries
// this renderer's channels are (view/Exposure.hpp). ITU-R BT.709-6, items 1.3
// and 1.4 of its colorimetry table.
constexpr exr_attr_chromaticities_t kRec709{
    .red_x = 0.640F,
    .red_y = 0.330F,
    .green_x = 0.300F,
    .green_y = 0.600F,
    .blue_x = 0.150F,
    .blue_y = 0.060F,
    .white_x = 0.3127F,
    .white_y = 0.3290F,
};

// A write context, released on a path that does not finish it. The success
// path calls exr_finish itself, because finishing a write is what writes the
// chunk table and closes the file, and its result is the point.
struct ExrContextRelease {
    void operator()(exr_context_t context) const noexcept {
        static_cast<void>(exr_finish(&context));
    }
};
using ExrContext = std::unique_ptr<std::remove_pointer_t<exr_context_t>, ExrContextRelease>;

// One scanline part: 16-bit float R, G and B, ZIP, its colours and a comment.
// OpenEXR sorts channels by name, so the order they are added in is free.
[[nodiscard]] std::expected<void, ImageFileError> describeExrImage(exr_context_t context,
                                                                   ImageSize size,
                                                                   std::string_view comment,
                                                                   const std::string& lastError) {
    int part = 0;
    if (auto ok = exrCheck(
            exr_add_part(context, "probe", EXR_STORAGE_SCANLINE, &part), "exr_add_part", lastError);
        !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = exrCheck(exr_initialize_required_attr_simple(context,
                                                               part,
                                                               static_cast<int32_t>(size.width),
                                                               static_cast<int32_t>(size.height),
                                                               EXR_COMPRESSION_ZIP),
                           "exr_initialize_required_attr_simple",
                           lastError);
        !ok) {
        return std::unexpected(ok.error());
    }
    // Linear light is not perceptually linear, so the hint a lossy compressor
    // reads says "logarithmic"; ZIP is lossless and ignores it.
    for (const char* name : {"R", "G", "B"}) {
        if (auto ok = exrCheck(
                exr_add_channel(
                    context, part, name, EXR_PIXEL_HALF, EXR_PERCEPTUALLY_LOGARITHMIC, 1, 1),
                "exr_add_channel",
                lastError);
            !ok) {
            return std::unexpected(ok.error());
        }
    }
    if (auto ok = exrCheck(exr_attr_set_chromaticities(context, part, "chromaticities", &kRec709),
                           "exr_attr_set_chromaticities",
                           lastError);
        !ok) {
        return std::unexpected(ok.error());
    }
    const std::string commentText(comment);
    if (auto ok = exrCheck(exr_attr_set_string(context, part, "comments", commentText.c_str()),
                           "exr_attr_set_string",
                           lastError);
        !ok) {
        return std::unexpected(ok.error());
    }
    return exrCheck(exr_write_header(context), "exr_write_header", lastError);
}

// Which of the four interleaved values a channel is, by its name.
[[nodiscard]] std::size_t channelOffset(std::string_view name) {
    if (name == "R") return 0;
    if (name == "G") return 1;
    ORBSIM_EXPECTS(name == "B"); // the only three describeExrImage adds
    return 2;
}

// Where in the interleaved image a run of pixels starts, and how long it is.
// A struct because both are counts, and reversed they would read a different
// run (non-negotiable 1).
struct PixelRun {
    std::size_t first{};
    std::size_t count{};
};

// One channel of a run of pixels, as OpenEXR reads it: planar, each value laid
// out as the machine holds a uint16_t. std::bit_cast gives exactly those
// bytes, whatever the machine's byte order, without a pointer cast.
[[nodiscard]] std::vector<std::uint8_t>
planarChannel(std::span<const std::uint16_t> rgbaHalf, std::size_t channel, PixelRun run) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(run.count * 2);
    const auto pixels =
        rgbaHalf.subspan(run.first * kChannelsPerPixel, run.count * kChannelsPerPixel);
    for (std::size_t i = channel; i < pixels.size(); i += kChannelsPerPixel) {
        const auto pair = std::bit_cast<std::array<std::uint8_t, 2>>(pixels.subspan(i, 1).front());
        bytes.insert(bytes.end(), pair.begin(), pair.end());
    }
    return bytes;
}

// OpenEXR's channel descriptions for the chunk being encoded.
[[nodiscard]] std::span<exr_coding_channel_info_t>
channelsOf(const exr_encode_pipeline_t& encoder) {
    // A pointer and a count, the C interface's shape; a span of the two is
    // what gives them bounds, and is the construction
    // -Wunsafe-buffer-usage-in-container reports, off for this line (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    return {encoder.channels, static_cast<std::size_t>(encoder.channel_count)};
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}

// A channel description that reads its values from `plane`, rows `width`
// pixels long, two bytes a value.
//
// **Rebuilt whole rather than assigned field by field** (register decision
// 204). The pointer lives in an anonymous union of the C struct, and naming it
// in an assignment is the union access cppcoreguidelines-pro-type-union-access
// reports; setting it in an aggregate initialiser is the one way the language
// gives to choose a union's member without accessing one. Every field is
// named, so -Wmissing-designated-field-initializers stops the build if
// OpenEXR ever adds one this does not copy.
// gcc's -Wmissing-braces wants the anonymous union's member braced, which a
// designated initialiser cannot spell; the union is OpenEXR's, so the
// warning is off for this function alone (ADR 0017).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif
[[nodiscard]] exr_coding_channel_info_t reading(const exr_coding_channel_info_t& channel,
                                                std::span<const std::uint8_t> plane,
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
        .encode_from_ptr = plane.data(),
    };
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// Every scanline chunk, top to bottom.
[[nodiscard]] std::expected<void, ImageFileError>
writeExrPixels(exr_context_t context,
               ImageSize size,
               std::span<const std::uint16_t> rgbaHalf,
               const std::string& lastError) {
    constexpr int kPart = 0;
    int32_t rowsPerChunk = 0;
    if (auto ok = exrCheck(exr_get_scanlines_per_chunk(context, kPart, &rowsPerChunk),
                           "exr_get_scanlines_per_chunk",
                           lastError);
        !ok) {
        return std::unexpected(ok.error());
    }
    ORBSIM_EXPECTS(rowsPerChunk > 0);
    // The pipeline lives on the stack; the guard frees what OpenEXR allocated
    // for it however the loop ends. A lambda rather than a class holding the
    // context, which would draw clang's lifetimebound suggestion for a
    // constructor parameter -- the macro for that lives in src/render
    // (view/Mat4.hpp's columnMajor gives way for the same reason).
    exr_encode_pipeline_t encoder{};
    const auto destroy = [context](exr_encode_pipeline_t* pipeline) noexcept {
        static_cast<void>(exr_encoding_destroy(context, pipeline));
    };
    std::unique_ptr<exr_encode_pipeline_t, decltype(destroy)> encoderGuard{nullptr, destroy};
    std::array<std::vector<std::uint8_t>, 3> planes;
    const auto height = static_cast<int32_t>(size.height);
    for (int32_t row = 0; row < height; row += rowsPerChunk) {
        exr_chunk_info_t chunk{};
        if (auto ok = exrCheck(exr_write_scanline_chunk_info(context, kPart, row, &chunk),
                               "exr_write_scanline_chunk_info",
                               lastError);
            !ok) {
            return std::unexpected(ok.error());
        }
        const bool first = encoderGuard == nullptr;
        const exr_result_t started = first
                                         ? exr_encoding_initialize(context, kPart, &chunk, &encoder)
                                         : exr_encoding_update(context, kPart, &chunk, &encoder);
        if (auto ok = exrCheck(started, "exr_encoding_initialize", lastError); !ok) {
            return std::unexpected(ok.error());
        }
        if (first) encoderGuard.reset(&encoder);

        const auto rows = static_cast<std::size_t>(std::min(rowsPerChunk, height - row));
        const PixelRun run{
            .first = static_cast<std::size_t>(row) * size.width,
            .count = rows * size.width,
        };
        for (auto&& [index, channel] : std::views::enumerate(channelsOf(encoder))) {
            const auto slot = static_cast<std::size_t>(index);
            planes.at(slot) = planarChannel(rgbaHalf, channelOffset(channel.channel_name), run);
            channel = reading(channel, planes.at(slot), size.width);
        }
        if (first) {
            if (auto ok = exrCheck(exr_encoding_choose_default_routines(context, kPart, &encoder),
                                   "exr_encoding_choose_default_routines",
                                   lastError);
                !ok) {
                return std::unexpected(ok.error());
            }
        }
        if (auto ok =
                exrCheck(exr_encoding_run(context, kPart, &encoder), "exr_encoding_run", lastError);
            !ok) {
            return std::unexpected(ok.error());
        }
    }
    return {};
}

} // namespace

std::expected<std::vector<std::byte>, ImageFileError>
encodePng8(ImageSize size, std::span<const std::uint8_t> rgba) {
    ORBSIM_EXPECTS(size.width > 0 && size.height > 0);
    ORBSIM_EXPECTS(rgba.size() == pixelCount(size) * kChannelsPerPixel);
    std::vector<unsigned char> rgb;
    rgb.reserve(pixelCount(size) * 3);
    for (std::size_t i = 0; i < rgba.size(); i += kChannelsPerPixel) {
        const auto pixel = rgba.subspan(i, 3);
        rgb.insert(rgb.end(), pixel.begin(), pixel.end());
    }
    return encodePngRgb(size, rgb, 8);
}

std::expected<std::vector<std::byte>, ImageFileError> encodePng8(const Rgb8Image& image) {
    std::vector<unsigned char> rgb(image.valueCount());
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        rgb.at(i) = image.value(i);
    }
    return encodePngRgb(image.size(), rgb, 8);
}

std::expected<Rgb8Image, PngReadFailure> decodePng8(std::span<const std::byte> png) {
    // The PNG's own layout first, so that what stb_image is handed is known
    // to be 8-bit RGB and it converts nothing (register decision 235).
    if (!hasPngHeader(png)) {
        return refuse(PngReadError::NotPng,
                      "it does not begin with the PNG signature and an IHDR chunk");
    }
    const auto bitDepth = std::to_integer<unsigned>(png.subspan(kIhdrBitDepthAt, 1).front());
    const auto colourType = std::to_integer<unsigned>(png.subspan(kIhdrColourTypeAt, 1).front());
    if (bitDepth != 8 || colourType != kPngColourTypeRgb) {
        return refuse(PngReadError::NotEightBitRgb,
                      std::format("its bit depth is {} and its colour type {}; a golden's are 8 "
                                  "and 2 (RGB)",
                                  bitDepth,
                                  colourType));
    }
    const std::uint32_t width = bigEndian32(png, kIhdrWidthAt);
    const std::uint32_t height = bigEndian32(png, kIhdrHeightAt);
    if (png.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return refuse(PngReadError::Undecodable, "it is larger than stb_image reads");
    }

    std::vector<stbi_uc> input(png.size());
    std::ranges::transform(
        png, input.begin(), [](std::byte b) noexcept { return std::to_integer<stbi_uc>(b); });
    int decodedWidth = 0;
    int decodedHeight = 0;
    int channelsInFile = 0;
    const std::unique_ptr<stbi_uc, StbFree> pixels{
        stbi_load_from_memory(input.data(),
                              static_cast<int>(input.size()),
                              &decodedWidth,
                              &decodedHeight,
                              &channelsInFile,
                              kRgbChannelsAsInt)};
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return refuse(PngReadError::Undecodable,
                      std::string("stb_image: ") +
                          (reason != nullptr ? reason : "no reason given"));
    }
    if (std::cmp_not_equal(decodedWidth, width) || std::cmp_not_equal(decodedHeight, height)) {
        return refuse(PngReadError::Undecodable,
                      std::format("stb_image decoded {}x{} where the header says {}x{}",
                                  decodedWidth,
                                  decodedHeight,
                                  width,
                                  height));
    }
    const ImageSize size{.width = width, .height = height};
    const std::size_t count = pixelCount(size) * kRgbChannels;
    // stb_image returns a pointer; its size is what it reported alongside. A
    // span of the two is the only way to give them bounds, which is the
    // construction -Wunsafe-buffer-usage-in-container reports (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<const stbi_uc> decoded(pixels.get(), count);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    auto image = Rgb8Image::from(size, std::vector<std::uint8_t>(decoded.begin(), decoded.end()));
    if (!image) return refuse(PngReadError::Undecodable, std::string(describe(image.error())));
    return *std::move(image);
}

std::expected<std::vector<std::byte>, ImageFileError>
encodePng16(ImageSize size, std::span<const std::uint16_t> rgba) {
    ORBSIM_EXPECTS(size.width > 0 && size.height > 0);
    ORBSIM_EXPECTS(rgba.size() == pixelCount(size) * kChannelsPerPixel);
    // PNG stores a 16-bit sample most significant byte first (PNG
    // specification, section 7.1), whatever the machine's order.
    std::vector<unsigned char> rgb;
    rgb.reserve(pixelCount(size) * 3 * 2);
    for (std::size_t i = 0; i < rgba.size(); i += kChannelsPerPixel) {
        for (const std::uint16_t sample : rgba.subspan(i, 3)) {
            rgb.push_back(static_cast<unsigned char>(sample >> 8U));
            rgb.push_back(static_cast<unsigned char>(sample & 0xFFU));
        }
    }
    return encodePngRgb(size, rgb, 16);
}

std::expected<void, ImageFileError> writeExr(const std::filesystem::path& path,
                                             ImageSize size,
                                             std::span<const std::uint16_t> rgbaHalf,
                                             std::string_view comment) {
    ORBSIM_EXPECTS(size.width > 0 && size.height > 0);
    ORBSIM_EXPECTS(rgbaHalf.size() == pixelCount(size) * kChannelsPerPixel);
    std::string lastError;

    // OpenEXR's own initialiser writes its pointer fields as 0, which gcc's
    // -Wzero-as-null-pointer-constant reports in our code where the macro is
    // expanded (clang treats the macro as the system header's). A library's
    // interface, so off at this line alone (ADR 0017).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
    exr_context_initializer_t init = EXR_DEFAULT_CONTEXT_INITIALIZER;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    init.error_handler_fn = onExrError;
    init.user_data = &lastError;

    const std::string utf8 = utf8Of(path);
    exr_context_t started = nullptr;
    if (auto ok = exrCheck(exr_start_write(&started, utf8.c_str(), EXR_WRITE_FILE_DIRECTLY, &init),
                           "exr_start_write",
                           lastError);
        !ok) {
        return std::unexpected(ok.error());
    }
    ExrContext context{started};
    if (auto ok = describeExrImage(context.get(), size, comment, lastError); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = writeExrPixels(context.get(), size, rgbaHalf, lastError); !ok) {
        return std::unexpected(ok.error());
    }
    exr_context_t finishing = context.release();
    return exrCheck(exr_finish(&finishing), "exr_finish", lastError);
}

std::expected<Rgb16Image, PngReadFailure> decodePng16(std::span<const std::byte> png) {
    // The same order as decodePng8: the PNG's own layout first, so that what
    // stb_image is handed is known to be 16-bit RGB and it converts nothing.
    if (!hasPngHeader(png)) {
        return refuse(PngReadError::NotPng,
                      "it does not begin with the PNG signature and an IHDR chunk");
    }
    const auto bitDepth = std::to_integer<unsigned>(png.subspan(kIhdrBitDepthAt, 1).front());
    const auto colourType = std::to_integer<unsigned>(png.subspan(kIhdrColourTypeAt, 1).front());
    if (bitDepth != 16 || colourType != kPngColourTypeRgb) {
        return refuse(PngReadError::NotSixteenBitRgb,
                      std::format("its bit depth is {} and its colour type {}; a probe's 16-bit "
                                  "image's are 16 and 2 (RGB)",
                                  bitDepth,
                                  colourType));
    }
    const std::uint32_t width = bigEndian32(png, kIhdrWidthAt);
    const std::uint32_t height = bigEndian32(png, kIhdrHeightAt);
    if (png.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return refuse(PngReadError::Undecodable, "it is larger than stb_image reads");
    }

    std::vector<stbi_uc> input(png.size());
    std::ranges::transform(
        png, input.begin(), [](std::byte b) noexcept { return std::to_integer<stbi_uc>(b); });
    int decodedWidth = 0;
    int decodedHeight = 0;
    int channelsInFile = 0;
    // stb_image's 16-bit decoder, which hands each sample back in the
    // machine's own byte order: the PNG's big-endian order is undone inside it.
    const std::unique_ptr<stbi_us, StbFree16> pixels{
        stbi_load_16_from_memory(input.data(),
                                 static_cast<int>(input.size()),
                                 &decodedWidth,
                                 &decodedHeight,
                                 &channelsInFile,
                                 kRgbChannelsAsInt)};
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return refuse(PngReadError::Undecodable,
                      std::string("stb_image: ") +
                          (reason != nullptr ? reason : "no reason given"));
    }
    if (std::cmp_not_equal(decodedWidth, width) || std::cmp_not_equal(decodedHeight, height)) {
        return refuse(PngReadError::Undecodable,
                      std::format("stb_image decoded {}x{} where the header says {}x{}",
                                  decodedWidth,
                                  decodedHeight,
                                  width,
                                  height));
    }
    const ImageSize size{.width = width, .height = height};
    const std::size_t count = pixelCount(size) * kRgbChannels;
    // A pointer and the size reported beside it, given bounds the one way
    // there is -- the construction decodePng8 makes, for its reason.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<const stbi_us> decoded(pixels.get(), count);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    auto image = Rgb16Image::from(size, std::vector<std::uint16_t>(decoded.begin(), decoded.end()));
    if (!image) return refuse(PngReadError::Undecodable, std::string(describe(image.error())));
    return *std::move(image);
}

} // namespace orb::view
