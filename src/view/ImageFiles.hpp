#ifndef ORBSIM_VIEW_IMAGEFILES_HPP
#define ORBSIM_VIEW_IMAGEFILES_HPP
//
// PNG and EXR files (M1-16; register decisions 191, 196, 197 and 204), and
// reading a golden image back (M1-17; register decision 235).
//
// **The three libraries are hidden behind this header.** lodepng writes both
// PNGs, OpenEXR's C library the EXR, and stb_image reads a golden; their
// headers are seen by view/ImageFiles.cpp and nothing else, as ERFA's are seen
// only by src/astro/*.cpp, so no other file of this project compiles against
// them. (The tests include stb_image themselves, to read lodepng's output
// with a library that did not write it.)
//
// **The PNGs as bytes, the EXR to a path.** lodepng's file writer takes a
// `char*` path in the Windows code page -- the failure CODING_GUIDELINES
// section 18 describes for a user whose name steps outside it -- so its PNGs
// come back as bytes and the application writes them through
// std::filesystem::path. OpenEXR is handed the path itself, as UTF-8, which it
// widens with MultiByteToWideChar(CP_UTF8) on Windows (internal_win32_file_
// impl.h): a write callback of our own would have had OpenEXR's fixed
// signature, two adjacent pointers and two adjacent 64-bit sizes, which
// bugprone-easily-swappable-parameters reports and no rewrite can avoid
// (register decision 204).
//
// **What is written, and why each is shaped so:**
//
//   * The PNGs are RGB. Alpha is dropped: every frame is opaque, and a
//     channel that carries nothing is a channel a viewer can get wrong.
//     8 bits per channel is what the window shows and what M1-17's goldens
//     compare; 16 bits is the same resolve at the precision the owner asked
//     for, so banding can be told apart from the 8-bit display's steps.
//   * The EXR holds the linear HDR target as binary16 -- the same bits the
//     GPU wrote, so it is an exact copy and not a conversion -- in channels
//     R, G and B, compressed losslessly (ZIP). It carries the chromaticities
//     of the Rec. 709 primaries and D65 white, which is what this renderer's
//     channels are (view/Exposure.hpp), so a viewer shows its colours right,
//     and a comment naming the unit.
//
#include "view/ImageCompare.hpp"
#include "view/ProbeImage.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orb::view {

// An encoder's refusal, in the library's own words: a PNG or EXR encoder
// fails for reasons it can explain (out of memory, a limit it enforces), and
// its explanation is the useful part -- the reason RenderError carries a
// string too (CODING_GUIDELINES section 7).
// gcc's -Wabi-tag: std::string carries libstdc++'s "cxx11" ABI tag, and
// gcc wants everything holding or returning one to carry it too. The tag
// guards code shipped as a binary against the old string ABI; this project
// builds everything from source with one ABI. Off at this site alone, for
// gcc alone -- register decision 52's ruling and shape.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
struct ImageFileError {
    std::string message;
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// An 8-bit PNG of an 8-bit RGB image: a golden, or a diff image (M1-17).
[[nodiscard]] std::expected<std::vector<std::byte>, ImageFileError>
encodePng8(const Rgb8Image& image);

// An 8-bit PNG from 8-bit RGBA, rows top to bottom. `rgba` must hold
// pixelCount(size) * 4 values; that is asserted, as encodeHdrDump asserts it.
[[nodiscard]] std::expected<std::vector<std::byte>, ImageFileError>
encodePng8(ImageSize size, std::span<const std::uint8_t> rgba);

// A 16-bit PNG from 16-bit RGBA, rows top to bottom, each value in the
// machine's own byte order as read back; the PNG itself is big-endian, as its
// specification requires, and the conversion happens here.
[[nodiscard]] std::expected<std::vector<std::byte>, ImageFileError>
encodePng16(ImageSize size, std::span<const std::uint16_t> rgba);

// An EXR at `path`, from the HDR target's binary16 RGBA, rows top to bottom.
// Alpha is dropped for the reason the PNGs drop it. `comment` becomes the
// file's standard "comments" attribute. A file already there is replaced; on
// failure OpenEXR removes what it wrote.
[[nodiscard]] std::expected<void, ImageFileError> writeExr(const std::filesystem::path& path,
                                                           ImageSize size,
                                                           std::span<const std::uint16_t> rgbaHalf,
                                                           std::string_view comment);

// --- reading a golden (M1-17; register decision 235) --------------------------

// Why a PNG was not read as a golden. Reported rather than asserted: a golden
// is a file, and a file can hold anything (ADR 0002).
enum class PngReadError : std::uint8_t {
    NotPng,         // the bytes do not begin with a PNG signature and header
    NotEightBitRgb, // a PNG, but not 8 bits per channel of R, G and B
    Undecodable,    // the header is right and the image data is not
};

[[nodiscard]] constexpr std::string_view describe(PngReadError error) noexcept {
    switch (error) {
    case PngReadError::NotPng:
        return "the file is not a PNG";
    case PngReadError::NotEightBitRgb:
        return "the PNG is not 8 bits per channel of R, G and B, which a golden is";
    case PngReadError::Undecodable:
        return "the PNG's image data could not be decoded";
    }
    return "unknown PNG reading error";
}

// The refusal, and what was found: a bit depth, a colour type, or the
// decoder's own words.
// gcc's -Wabi-tag: std::string carries libstdc++'s "cxx11" ABI tag, and
// gcc wants everything holding or returning one to carry it too. The tag
// guards code shipped as a binary against the old string ABI; this project
// builds everything from source with one ABI. Off at this site alone, for
// gcc alone -- register decision 52's ruling and shape.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
struct PngReadFailure {
    PngReadError error{};
    std::string detail;
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// An 8-bit RGB PNG, decoded. **Nothing else is read, and nothing is
// converted**: the header's bit depth and colour type are checked from the PNG
// specification's layout before stb_image decodes, so a 16-bit, grey,
// palette or alpha PNG is refused by name rather than turned into something
// that is not what was committed.
[[nodiscard]] std::expected<Rgb8Image, PngReadFailure> decodePng8(std::span<const std::byte> png);

} // namespace orb::view

#endif // ORBSIM_VIEW_IMAGEFILES_HPP
