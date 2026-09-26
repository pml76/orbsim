#include "HdrDumpFile.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Scalar.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace orb::test {
namespace {

constexpr std::string_view kMagic = "orbsim-hdr-f32 ";
constexpr std::string_view kVersion = "v1 ";
constexpr std::size_t kValuesPerPixel = 4;
constexpr std::size_t kBytesPerValue = 4;

// A dimension as the format writes it: one or more decimal digits, no sign,
// no leading zero, positive, and at most kMaxDimension.
[[nodiscard]] std::optional<std::uint32_t> parseDimension(std::string_view text) {
    if (text.empty() || text.front() == '0') return std::nullopt;
    if (!std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const char* const last = std::to_address(text.end());
    const auto [end, ec] = std::from_chars(std::to_address(text.begin()), last, value);
    if (ec != std::errc{} || end != last || value > kMaxDimension) return std::nullopt;
    return value;
}

// The header line's text, without its LF, or nothing if there is no LF early
// enough to be a header. Copied into a string rather than viewed in place,
// which would need a cast from bytes to characters; it is at most
// kMaxHeaderBytes long.
[[nodiscard]] std::optional<std::string> headerLine(std::span<const std::byte> bytes) {
    const std::span<const std::byte> window = bytes.first(std::min(bytes.size(), kMaxHeaderBytes));
    const auto newline = std::ranges::find(window, std::byte{'\n'});
    if (newline == window.end()) return std::nullopt;
    std::string text;
    std::ranges::transform(window.begin(), newline, std::back_inserter(text), [](std::byte b) {
        return static_cast<char>(b);
    });
    return text;
}

// One little-endian binary32, assembled byte by byte, so the reader does not
// depend on the byte order of the machine running the test.
[[nodiscard]] f32 littleEndianFloat(std::span<const std::byte, kBytesPerValue> bytes) {
    std::uint32_t bits = 0;
    // The last byte is the most significant, so the bytes are read backwards.
    for (const std::byte b : std::views::reverse(bytes)) {
        bits = (bits << 8U) | std::to_integer<std::uint32_t>(b);
    }
    return std::bit_cast<f32>(bits);
}

} // namespace

f32 HdrDump::at(Pixel pixel, Channel channel) const {
    ORBSIM_EXPECTS(pixel.column < width_ && pixel.row < height_);
    const std::size_t pixelIndex = (static_cast<std::size_t>(pixel.row) * width_) + pixel.column;
    const std::size_t index = (pixelIndex * kValuesPerPixel) + static_cast<std::size_t>(channel);
    return values_.at(index);
}

std::expected<HdrDump, HdrDumpError> parseHdrDump(std::span<const std::byte> bytes) {
    const std::optional<std::string> line = headerLine(bytes);
    if (!line) return std::unexpected(HdrDumpError::NoHeaderLine);
    std::string_view rest = *line;
    if (!rest.starts_with(kMagic)) return std::unexpected(HdrDumpError::WrongMagic);
    rest.remove_prefix(kMagic.size());
    if (!rest.starts_with(kVersion)) return std::unexpected(HdrDumpError::UnsupportedVersion);
    rest.remove_prefix(kVersion.size());

    const std::size_t space = rest.find(' ');
    if (space == std::string_view::npos) return std::unexpected(HdrDumpError::BadDimensions);
    const std::optional<std::uint32_t> width = parseDimension(rest.substr(0, space));
    const std::optional<std::uint32_t> height = parseDimension(rest.substr(space + 1));
    if (!width || !height) return std::unexpected(HdrDumpError::BadDimensions);

    const std::size_t valueCount = static_cast<std::size_t>(*width) * *height * kValuesPerPixel;
    const std::span<const std::byte> data = bytes.subspan(line->size() + 1);
    if (data.size() != valueCount * kBytesPerValue) {
        return std::unexpected(HdrDumpError::WrongDataLength);
    }

    HdrDump dump;
    dump.width_ = *width;
    dump.height_ = *height;
    dump.values_.reserve(valueCount);
    for (std::size_t i = 0; i < valueCount; ++i) {
        dump.values_.push_back(
            littleEndianFloat(data.subspan(i * kBytesPerValue).first<kBytesPerValue>()));
    }
    return dump;
}

std::expected<HdrDump, HdrDumpError> readHdrDump(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(HdrDumpError::FileNotFound);
    std::vector<char> text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return parseHdrDump(bytes);
}

} // namespace orb::test
