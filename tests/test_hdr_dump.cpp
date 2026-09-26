//
// Tests for tests/HdrDumpFile.hpp: the reader of a probe's linear HDR dump
// (M1-16, register decision 191).
//
// **Every byte below is written by hand**, from the format the reader's
// header states, and none comes from the writer in view/ProbeImage.hpp -- so
// a misreading of the format shared by both cannot pass here. The last case
// puts the writer's output beside those hand-written bytes, which is what
// ties the two implementations to the one statement of the format. The binary32
// values are the IEEE 754 encodings of numbers that are exact in binary
// (1, -2, 0.5, ...), little-endian, least significant byte first.
//
// **Every way a dump can be wrong is asked for by name**, in the shape M1-06
// set for the fixture reader: a check for one failure must be seen to be the
// one that fired, or a test for "refuses bad input" passes on the wrong
// refusal.
//
#include "HdrDumpFile.hpp"
#include "core/Scalar.hpp"
#include "view/ProbeImage.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace orb;
using namespace orb::test;

namespace {

// The bytes of `text` followed by `data`, as a file would hold them.
[[nodiscard]] std::vector<std::byte> fileOf(std::string_view text,
                                            std::initializer_list<unsigned> data = {}) {
    std::vector<std::byte> bytes;
    for (const char c : text) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    for (const unsigned b : data) {
        bytes.push_back(static_cast<std::byte>(b));
    }
    return bytes;
}

// Eight binary32 values, little-endian: a 2x1 image.
//   pixel (0, 0): 1.0, -2.0, 0.5, 1.0
//   pixel (1, 0): 0.0, 65504.0, 0x1p-24, -0.0
const std::initializer_list<unsigned> kTwoPixels = {
    0x00, 0x00, 0x80, 0x3F, // 1.0      = 0x3F800000
    0x00, 0x00, 0x00, 0xC0, // -2.0     = 0xC0000000
    0x00, 0x00, 0x00, 0x3F, // 0.5      = 0x3F000000
    0x00, 0x00, 0x80, 0x3F, // 1.0
    0x00, 0x00, 0x00, 0x00, // 0.0
    0x00, 0xE0, 0x7F, 0x47, // 65504.0  = 0x477FE000
    0x00, 0x00, 0x80, 0x33, // 2^-24    = 0x33800000
    0x00, 0x00, 0x00, 0x80, // -0.0     = 0x80000000
};

// Parses and requires the named refusal, having first required a refusal at
// all: reading error() from an expected that holds a value is undefined, and
// once let a refusal test pass while the value was accepted (VERIFICATION.md
// rule 23, M1-87).
void requireRefused(const std::vector<std::byte>& file, HdrDumpError expected) {
    const auto dump = parseHdrDump(file);
    INFO("expected: " << describe(expected));
    REQUIRE_FALSE(dump.has_value());
    REQUIRE(dump.error() == expected);
}

} // namespace

TEST_CASE("a well-formed dump is read value for value") {
    const auto dump = parseHdrDump(fileOf("orbsim-hdr-f32 v1 2 1\n", kTwoPixels));
    REQUIRE(dump.has_value());
    REQUIRE(dump->width() == 2U);
    REQUIRE(dump->height() == 1U);
    using C = HdrDump::Channel;
    REQUIRE(bitsOf(dump->at({.column = 0, .row = 0}, C::Red)) == bitsOf(1.0F));
    REQUIRE(bitsOf(dump->at({.column = 0, .row = 0}, C::Green)) == bitsOf(-2.0F));
    REQUIRE(bitsOf(dump->at({.column = 0, .row = 0}, C::Blue)) == bitsOf(0.5F));
    REQUIRE(bitsOf(dump->at({.column = 0, .row = 0}, C::Alpha)) == bitsOf(1.0F));
    REQUIRE(bitsOf(dump->at({.column = 1, .row = 0}, C::Red)) == bitsOf(0.0F));
    REQUIRE(bitsOf(dump->at({.column = 1, .row = 0}, C::Green)) == bitsOf(65504.0F));
    REQUIRE(bitsOf(dump->at({.column = 1, .row = 0}, C::Blue)) == bitsOf(0x1p-24F));
    REQUIRE(bitsOf(dump->at({.column = 1, .row = 0}, C::Alpha)) == bitsOf(-0.0F));
}

TEST_CASE("rows run top to bottom") {
    // A 1x2 image: the first four values are the top row.
    const auto dump = parseHdrDump(fileOf("orbsim-hdr-f32 v1 1 2\n", kTwoPixels));
    REQUIRE(dump.has_value());
    using C = HdrDump::Channel;
    REQUIRE(bitsOf(dump->at({.column = 0, .row = 0}, C::Green)) == bitsOf(-2.0F));
    REQUIRE(bitsOf(dump->at({.column = 0, .row = 1}, C::Green)) == bitsOf(65504.0F));
}

TEST_CASE("a file with no header line is refused by name") {
    requireRefused(fileOf(""), HdrDumpError::NoHeaderLine);
    requireRefused(fileOf("orbsim-hdr-f32 v1 2 1"), HdrDumpError::NoHeaderLine);
    // The LF must fall inside the first kMaxHeaderBytes bytes: as the last of
    // them it ends a header (which then fails on its magic), one byte later it
    // does not.
    const std::string lastInWindow = std::string(kMaxHeaderBytes - 1, 'x') + "\n";
    const std::string oneLate = std::string(kMaxHeaderBytes, 'x') + "\n";
    requireRefused(fileOf(lastInWindow), HdrDumpError::WrongMagic);
    requireRefused(fileOf(oneLate), HdrDumpError::NoHeaderLine);
}

TEST_CASE("a header that is not this format's is refused by name") {
    requireRefused(fileOf("orbsim-hdr-f16 v1 2 1\n", kTwoPixels), HdrDumpError::WrongMagic);
    requireRefused(fileOf("ORBSIM-HDR-F32 v1 2 1\n", kTwoPixels), HdrDumpError::WrongMagic);
    requireRefused(fileOf("orbsim-hdr-f32 v2 2 1\n", kTwoPixels), HdrDumpError::UnsupportedVersion);
    requireRefused(fileOf("orbsim-hdr-f32 2 1\n", kTwoPixels), HdrDumpError::UnsupportedVersion);
}

TEST_CASE("dimensions that are not two positive decimal integers are refused by name") {
    for (const std::string_view header : {
             "orbsim-hdr-f32 v1 0 1\n",     // zero
             "orbsim-hdr-f32 v1 02 1\n",    // a leading zero
             "orbsim-hdr-f32 v1 +2 1\n",    // a sign
             "orbsim-hdr-f32 v1 -2 1\n",    // a negative sign
             "orbsim-hdr-f32 v1 2\n",       // one dimension
             "orbsim-hdr-f32 v1 2 1 1\n",   // three
             "orbsim-hdr-f32 v1 2  1\n",    // two spaces
             "orbsim-hdr-f32 v1 2x 1\n",    // not a number
             "orbsim-hdr-f32 v1 2 1\r\n",   // CRLF: the format's line ends in LF alone
             "orbsim-hdr-f32 v1 16385 1\n", // over the limit
             "orbsim-hdr-f32 v1 99999999999999999999 1\n", // over any integer
         }) {
        INFO("header: " << header);
        requireRefused(fileOf(header, kTwoPixels), HdrDumpError::BadDimensions);
    }
}

TEST_CASE("the largest dimension the limit allows is accepted as a header") {
    // 16384 x 1 needs 262,144 bytes of data; with none the header is still
    // read, and it is the data length that is refused -- so the limit is
    // inclusive, and the refusal above is the limit's rather than the length's.
    requireRefused(fileOf("orbsim-hdr-f32 v1 16384 1\n"), HdrDumpError::WrongDataLength);
}

TEST_CASE("data too short or too long is refused by name") {
    std::vector<std::byte> shortFile = fileOf("orbsim-hdr-f32 v1 2 1\n", kTwoPixels);
    shortFile.pop_back();
    requireRefused(shortFile, HdrDumpError::WrongDataLength);

    std::vector<std::byte> longFile = fileOf("orbsim-hdr-f32 v1 2 1\n", kTwoPixels);
    longFile.push_back(std::byte{0});
    requireRefused(longFile, HdrDumpError::WrongDataLength);

    requireRefused(fileOf("orbsim-hdr-f32 v1 1 1\n", kTwoPixels), HdrDumpError::WrongDataLength);
}

TEST_CASE("a missing file is refused by name") {
    const auto dump = readHdrDump(std::filesystem::path{"no/such/directory/clear.hdr.f32"});
    REQUIRE_FALSE(dump.has_value());
    REQUIRE(dump.error() == HdrDumpError::FileNotFound);
}

TEST_CASE("the application's writer and this reader agree") {
    // Two implementations of one format, written apart (VERIFICATION.md rule
    // 2): view/ProbeImage.hpp's encodeHdrDump and the reader above. The
    // binary16 inputs are chosen so their binary32 values are known without
    // either: 1, -2, 0.5, 65504, 2^-24 and -0.
    const std::vector<std::uint16_t> halves{
        0x3C00,
        0xC000,
        0x3800,
        0x3C00,
        0x0000,
        0x7BFF,
        0x0001,
        0x8000,
    };
    const std::vector<std::byte> file = orb::view::encodeHdrDump({.width = 2, .height = 1}, halves);
    REQUIRE(file == fileOf("orbsim-hdr-f32 v1 2 1\n", kTwoPixels));
}
