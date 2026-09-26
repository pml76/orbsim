#include "view/ImageFiles.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "view/ProbeImage.hpp"

#include <lodepng.h>
#include <openexr_attr.h>
#include <openexr_chunkio.h>
#include <openexr_coding.h>
#include <openexr_context.h>
#include <openexr_encode.h>
#include <openexr_errors.h>
#include <openexr_part.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
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
    std::vector<unsigned char> png;
    const unsigned error = lodepng::encode(png, rgb, size.width, size.height, LCT_RGB, bitDepth);
    if (error != 0) return fail(std::string("PNG encoding failed: ") + lodepng_error_text(error));
    std::vector<std::byte> bytes(png.size());
    std::ranges::transform(
        png, bytes.begin(), [](unsigned char c) noexcept { return std::byte{c}; });
    return bytes;
}

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

} // namespace orb::view
