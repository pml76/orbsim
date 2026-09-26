#ifndef ORBSIM_TESTS_HDRDUMPFILE_HPP
#define ORBSIM_TESTS_HDRDUMPFILE_HPP
//
// The reader for a probe's linear HDR dump, `<name>.hdr.f32` (M1-16, register
// decision 191). Used by the tests -- M1-16's check of the `clear` probe and
// M1-18's radiometry -- and never by the application.
//
// **The format**, stated here once because this is the one reader of it:
//
//     orbsim-hdr-f32 v1 <width> <height>\n
//     width * height * 4 IEEE 754 binary32 values, little-endian,
//     R G B A per pixel, pixels left to right, rows top to bottom
//
// The line is ASCII: the magic, a version, and the two dimensions as decimal
// integers without leading zeros or sign, each separated by one space and
// ended by one LF. Nothing follows the last value.
//
// **Written independently of the writer**, view/ProbeImage.hpp's
// encodeHdrDump, from the description above rather than from that code, so
// that a mistake in one does not hide behind the same mistake in the other
// (VERIFICATION.md rule 2). The malformed-input tests follow M1-06's shape:
// every way a file can be wrong has a name, and each is asked for by name.
//
#include "core/Scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace orb::test {

// One name per way a dump can be wrong.
enum class HdrDumpError : std::uint8_t {
    FileNotFound,       // no readable file at that path
    NoHeaderLine,       // no LF within the first kMaxHeaderBytes bytes
    WrongMagic,         // the line does not start "orbsim-hdr-f32 "
    UnsupportedVersion, // a version other than v1
    BadDimensions,      // not two positive decimal integers within the limit
    WrongDataLength,    // the values that follow are too few or too many
};

[[nodiscard]] constexpr std::string_view describe(HdrDumpError error) noexcept {
    switch (error) {
    case HdrDumpError::FileNotFound:
        return "there is no readable file at that path";
    case HdrDumpError::NoHeaderLine:
        return "the file does not start with a header line";
    case HdrDumpError::WrongMagic:
        return "the header line does not start with orbsim-hdr-f32";
    case HdrDumpError::UnsupportedVersion:
        return "the header names a version this reader does not know";
    case HdrDumpError::BadDimensions:
        return "the header does not give two positive dimensions within the limit";
    case HdrDumpError::WrongDataLength:
        return "the data is not exactly width * height * 4 binary32 values";
    }
    return "unknown HDR dump error";
}

// Longer than any valid header line, which is at most 31 bytes; a file with
// no LF this early is not a dump, and the reader says so rather than
// scanning megabytes of binary for one.
inline constexpr std::size_t kMaxHeaderBytes = 64;

// Each dimension is refused above this. A probe frame is 1280x720; the limit
// is what keeps width * height * 16 far inside std::size_t on every platform
// this project builds for, so a malicious header cannot overflow it.
inline constexpr std::uint32_t kMaxDimension = 16'384;

// Where in the image. A struct rather than two parameters, because a column
// and a row are both whole numbers and reversed they read a different pixel
// (non-negotiable 1).
struct Pixel {
    std::uint32_t column{};
    std::uint32_t row{}; // 0 is the top
};

// A dump, read. Values are held as the file holds them.
class HdrDump {
public:
    // Only parseHdrDump builds one, so width, height and the number of values
    // always agree.
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    // Which of the four values of a pixel.
    enum class Channel : std::uint8_t { Red, Green, Blue, Alpha };

    // The value at one pixel. Asserted in range: a test asking outside the
    // image is a defect in the test.
    [[nodiscard]] f32 at(Pixel pixel, Channel channel) const;

    friend std::expected<HdrDump, HdrDumpError> parseHdrDump(std::span<const std::byte> bytes);

private:
    HdrDump() = default;

    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<f32> values_;
};

[[nodiscard]] std::expected<HdrDump, HdrDumpError> parseHdrDump(std::span<const std::byte> bytes);
[[nodiscard]] std::expected<HdrDump, HdrDumpError> readHdrDump(const std::filesystem::path& path);

} // namespace orb::test

#endif // ORBSIM_TESTS_HDRDUMPFILE_HPP
