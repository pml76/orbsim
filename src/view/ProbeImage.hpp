#ifndef ORBSIM_VIEW_PROBEIMAGE_HPP
#define ORBSIM_VIEW_PROBEIMAGE_HPP
//
// The images a probe writes, as bytes (M1-16; register decisions 187-199).
//
// **Everything here is headless**, which is why it is in orbsim_view and not
// in src/render: the renderer reads a frame back and hands the pixels over,
// and turning them into files needs no device, so it is tested without one
// (ADR 0012). The application writes the bytes to disk.
//
// **Four encodings of one frame.** The linear HDR target, read back as
// binary16, becomes the raw dump M1-18's tests read (`<name>.hdr.f32`, here)
// and an EXR a person can open in an HDR viewer (`<name>.exr`); the resolve
// pass's output, drawn once at 8 and once at 16 bits per channel, becomes two
// PNGs (`<name>.png`, `<name>.16.png`). The PNG and EXR encoders are in
// view/ImageFiles.hpp, which is where the two libraries they need are hidden.
//
// **The dump format** is stated where it is read, tests/HdrDumpFile.hpp:
//
//     orbsim-hdr-f32 v1 <width> <height>\n
//     width * height * 4 binary32, little-endian, RGBA, rows top to bottom
//
// This writer is written from that statement, and the reader from the same
// statement separately, so that the two cannot share a misreading of it.
//
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orb::view {

// An image's size in pixels. A struct, so that width and height are named at
// every construction site (non-negotiable 1).
struct ImageSize {
    std::uint32_t width{};
    std::uint32_t height{};
};

// Free rather than a member: a member function would make
// misc-non-private-member-variables-in-classes report the two public fields
// (the trap PROJECT_STATE.md section 8 records).
[[nodiscard]] constexpr std::size_t pixelCount(ImageSize size) noexcept {
    return static_cast<std::size_t>(size.width) * size.height;
}

// Every probe frame's size, whatever the window's (ADR 0008). 16:9, and small
// enough that a frame and its PNGs are read in a moment.
inline constexpr ImageSize kProbeImageSize{.width = 1280, .height = 720};

// Four channels -- R, G, B, A -- in every buffer a frame is read back into.
inline constexpr std::size_t kChannelsPerPixel = 4;

// The dump's header line, LF included: "orbsim-hdr-f32 v1 1280 720\n".
[[nodiscard]] std::string hdrDumpHeader(ImageSize size);

// The whole dump: the header, then every binary16 value of `rgbaHalf`
// converted exactly to binary32 (view/Half.hpp) and written little-endian.
//
// `rgbaHalf` is the HDR target as read back -- binary16 bit patterns, RGBA,
// rows top to bottom -- and must hold exactly pixelCount(size) * 4 values.
// That is asserted: the readback that produces it sizes it from the same
// ImageSize, so a mismatch is a defect here, not an input to report (ADR 0002).
[[nodiscard]] std::vector<std::byte> encodeHdrDump(ImageSize size,
                                                   std::span<const std::uint16_t> rgbaHalf);

} // namespace orb::view

#endif // ORBSIM_VIEW_PROBEIMAGE_HPP
