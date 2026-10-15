//
// Tests for tests/TestReads.hpp and tests/Sha256.hpp (M1-129, register
// decisions 527-530): the hash behind each read record, and the two readers
// that refuse a path they should not read.
//
// The hash's short examples and the record's file names are proved at
// compile time, in tests/TestReads.cpp. What is here is what a static_assert
// cannot reach: the million-byte example, and the file system.
//
// What a record holds, and that a skipped test is skipped for the right
// reason, is the test driver's to show: scripts/run-tests.py --self-test, and
// the end-to-end test that runs it on a scratch project.
//
#include "tests/Sha256.hpp"
#include "tests/TestCase.hpp"
#include "tests/TestReads.hpp"

#include <catch2/catch_message.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace orb::test;

namespace {

[[nodiscard]] std::string hexText(const Sha256Digest& digest) {
    const std::array<char, 64> hex = hexOf(digest);
    return {hex.data(), hex.size()};
}

// The system's temporary directory, absolute, without a trailing separator.
[[nodiscard]] std::filesystem::path temporaryDirectory() {
    std::filesystem::path folder =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    if (folder.filename().empty()) folder = folder.parent_path();
    return folder;
}

} // namespace

TEST_CASE("SHA-256 of a million a's is FIPS 180-2's third example", "[read-records][sha256]") {
    const std::vector<std::byte> message(1'000'000, std::byte{'a'});
    CHECK(hexText(sha256(std::span<const std::byte>{message})) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 of every byte value is hashlib's", "[read-records][sha256]") {
    // Python 3: hashlib.sha256(bytes(range(256))).hexdigest(), 2026-10-14.
    std::vector<std::byte> message;
    message.reserve(256);
    for (unsigned value = 0; value < 256; ++value) {
        message.push_back(static_cast<std::byte>(value));
    }
    CHECK(hexText(sha256(std::span<const std::byte>{message})) ==
          "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
}

TEST_CASE("a recorded data file reads back whole", "[read-records]") {
    const std::string_view file = dataFile("skyfield/README.md");
    const auto bytes = readRecordedFile(file);
    INFO("reading " << file << ": " << (bytes.has_value() ? "ok" : describe(bytes.error())));
    REQUIRE(bytes.has_value());
    // Held to the SHA-256 CMake computed of the same file (register decision
    // 554), set beside this suite in CMakeLists.txt.
    CHECK(hexText(sha256(std::span<const std::byte>{*bytes})) == ORBSIM_READ_BACK_SHA256);
}

TEST_CASE("a path nothing recorded is refused, however it is spelled", "[read-records]") {
    // The same file dataFile() names, spelled as the test would spell it
    // without the helper: refused, so a test cannot read data/ unrecorded
    // through readRecordedFile.
    const std::filesystem::path recorded{dataFile("skyfield/README.md")};
    const std::string spelledByHand = (recorded.parent_path() / "." / "README.md").string();
    const auto refused = readRecordedFile(spelledByHand);
    REQUIRE(!refused.has_value());
    CHECK(refused.error() == FileReadError::NotRecorded);
}

TEST_CASE("a recorded file that is not there is CannotOpen", "[read-records]") {
    const auto missing = readRecordedFile(dataFile("skyfield/no-such-file.txt"));
    REQUIRE(!missing.has_value());
    CHECK(missing.error() == FileReadError::CannotOpen);
}

namespace {

// The scratch folder the two cases below use; neither needs it to exist
// except where it writes into it.
[[nodiscard]] std::filesystem::path scratchFolder() {
    return temporaryDirectory() / "orbsim_test_read_records";
}

} // namespace

TEST_CASE("a scratch file reads back", "[read-records][scratch]") {
    const std::filesystem::path folder = scratchFolder();
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "written.bin";
    {
        std::ofstream out{file, std::ios::binary};
        out << "written by this case";
    }

    const auto inside = readScratchFile(file);
    REQUIRE(inside.has_value());
    CHECK(inside->size() == std::string_view{"written by this case"}.size());

    const auto missing = readScratchFile(folder / "never-written.bin");
    REQUIRE(!missing.has_value());
    CHECK(missing.error() == FileReadError::CannotOpen);
    std::filesystem::remove_all(folder);
}

TEST_CASE("nothing outside the scratch folder reads back", "[read-records][scratch]") {
    const std::filesystem::path folder = scratchFolder();
    // Outside: a file in the source tree; a sibling folder whose name begins
    // with the scratch folder's; and a path that climbs out with `..`.
    const std::filesystem::path sibling{temporaryDirectory().string() + "2"};
    for (const std::filesystem::path& outside : {
             std::filesystem::path{dataFile("skyfield/README.md")},
             sibling / "written.bin",
             folder / ".." / ".." / "written.bin",
         }) {
        const auto refused = readScratchFile(outside);
        INFO("reading " << outside.string());
        REQUIRE(!refused.has_value());
        CHECK(refused.error() == FileReadError::OutsideScratch);
    }
}

// Run alone by the CTest test read_record_written (cmake/VerifyReadRecord.cmake),
// which then reads the record this case leaves and holds it to what it must
// say: the file name escaped as register decision 528 spells it, each file
// once, the hash CMake's own file(SHA256) gives, `missing` for a file that is
// not there, and the last line `end`. Hidden from discovery by its tag, so
// that no other process ever writes the same record at the same time.
TEST_CASE("record check: a/b <c>", "[.read-record-check]") {
    static_cast<void>(dataFile("skyfield/README.md"));
    static_cast<void>(dataFile("skyfield/no-such-file.txt"));
    static_cast<void>(dataFile("skyfield/README.md"));
    SUCCEED("recorded");
}
