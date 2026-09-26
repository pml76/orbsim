#include "view/ProbeImage.hpp" // SF.5: own header, first
#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "view/Half.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace orb::view {

std::string hdrDumpHeader(ImageSize size) {
    return std::format("orbsim-hdr-f32 v1 {} {}\n", size.width, size.height);
}

std::vector<std::byte> encodeHdrDump(ImageSize size, std::span<const std::uint16_t> rgbaHalf) {
    ORBSIM_EXPECTS(size.width > 0 && size.height > 0);
    ORBSIM_EXPECTS(rgbaHalf.size() == pixelCount(size) * kChannelsPerPixel);

    const std::string header = hdrDumpHeader(size);
    std::vector<std::byte> bytes;
    bytes.reserve(header.size() + (rgbaHalf.size() * sizeof(f32)));
    for (const char c : header) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    // Little-endian by construction, least significant byte first, rather
    // than by copying the machine's own representation: the file then means
    // the same thing whichever machine wrote it.
    for (const std::uint16_t half : rgbaHalf) {
        const std::uint32_t bits = bitsOf(halfToFloat(half));
        bytes.push_back(static_cast<std::byte>(bits & 0xFFU));
        bytes.push_back(static_cast<std::byte>((bits >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::byte>((bits >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::byte>(bits >> 24U));
    }
    return bytes;
}

} // namespace orb::view
