//
// Tests for view/FileWrite.hpp: writing a file, and replacing one without
// ever leaving half of it (M1-108; register decisions 232, 387 and 388), and
// writing text, with a refusal at close reported (M1-111).
//
// **Never through --accept-golden**, which no test may run (decision 233):
// replaceFile is the rule that option relies on, moved where a test can reach
// it, and these cases hold it on a folder of their own in the system's
// temporary directory, one per case, so that cases CTest runs in parallel
// never share one.
//
// **Read back by this file's own reader**, a byte at a time through
// std::ifstream, never by anything view/FileWrite.cpp uses. The bytes are
// every value from 0 to 255, so a write in text mode -- which on Windows turns
// 0x0A into 0x0D 0x0A and stops at nothing -- comes back a different length.
//
#include "view/FileWrite.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace orb::view;

namespace {

// An empty folder of this case's own, emptied first in case an earlier run
// stopped half way.
[[nodiscard]] std::filesystem::path scratchFolder(std::string_view name) {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("orbsim_test_file_replace_" + std::string(name));
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

// Every byte value, in order, then again backwards: 512 bytes holding each of
// 0x0A, 0x0D and 0x1A twice.
[[nodiscard]] std::vector<std::byte> everyByte() {
    std::vector<std::byte> bytes;
    bytes.reserve(512);
    for (unsigned value = 0; value < 256; ++value) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    for (unsigned value = 256; value > 0; --value) {
        bytes.push_back(static_cast<std::byte>(value - 1));
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> bytes;
    for (const char c : text) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> contentsOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in);
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) {
        bytes.push_back(static_cast<std::byte>(*it));
    }
    return bytes;
}

[[nodiscard]] std::filesystem::path partialOf(const std::filesystem::path& target) {
    std::filesystem::path partial = target;
    partial += ".partial";
    return partial;
}

} // namespace

TEST_CASE("writeFile writes every byte value as it is") {
    const std::filesystem::path folder = scratchFolder("write");
    const std::filesystem::path file = folder / "bytes.bin";

    REQUIRE(writeFile(file, everyByte()));
    CHECK(contentsOf(file) == everyByte());

    std::filesystem::remove_all(folder);
}

TEST_CASE("writeFile replaces a longer file rather than writing into it") {
    const std::filesystem::path folder = scratchFolder("write_shorter");
    const std::filesystem::path file = folder / "bytes.bin";
    REQUIRE(writeFile(file, everyByte()));

    REQUIRE(writeFile(file, bytesOf("short")));
    CHECK(contentsOf(file) == bytesOf("short"));

    std::filesystem::remove_all(folder);
}

// Line endings among them: in text mode on Windows, "\n" would come back as
// "\r\n" and the length would differ (M1-111).
TEST_CASE("writeText writes the text's characters as they are") {
    const std::filesystem::path folder = scratchFolder("write_text");
    const std::filesystem::path file = folder / "report.csv";
    constexpr std::string_view kText = "frame,phase\n0,warm-up\r\n1,measured\n\x1A tail";

    REQUIRE(writeText(file, kText));
    CHECK(contentsOf(file) == bytesOf(kText));

    std::filesystem::remove_all(folder);
}

// A few bytes stay in the stream's buffer until the file is closed, so a disk
// that refuses them refuses only then -- and a write that checked before the
// close reported success (M1-111, measured with this very case). /dev/full
// refuses every write as a full disk would; Windows has no such device, so
// there the case reports itself skipped rather than passing.
TEST_CASE("a write the disk refuses only when the file is closed is reported") {
    const std::filesystem::path full{"/dev/full"};
    if (!std::filesystem::exists(full)) {
        SKIP("this platform has no /dev/full, the device that refuses every write as a "
             "full disk would, so a refusal at close cannot be produced here");
    }

    const auto written = writeFile(full, bytesOf("short"));
    REQUIRE_FALSE(written);
    // Named by the file it could not write.
    CHECK(written.error().contains("/dev/full"));
}

TEST_CASE("replaceFile puts a file in place where there was none, and nothing beside it") {
    const std::filesystem::path folder = scratchFolder("new");
    const std::filesystem::path target = folder / "golden.png";

    REQUIRE(replaceFile(target, everyByte()));
    CHECK(contentsOf(target) == everyByte());
    CHECK_FALSE(std::filesystem::exists(partialOf(target)));
    // The folder holds the target and nothing else.
    CHECK(std::distance(std::filesystem::directory_iterator(folder),
                        std::filesystem::directory_iterator()) == 1);

    std::filesystem::remove_all(folder);
}

TEST_CASE("replaceFile puts an empty file in place") {
    const std::filesystem::path folder = scratchFolder("empty");
    const std::filesystem::path target = folder / "golden.png";

    REQUIRE(replaceFile(target, {}));
    // Read back rather than asked for its size: std::filesystem::file_size
    // is where clang-analyzer reports a false finding inside MSVC's own
    // header, and reading needs nothing view/FileWrite.cpp uses.
    CHECK(contentsOf(target).empty());
    CHECK_FALSE(std::filesystem::exists(partialOf(target)));

    std::filesystem::remove_all(folder);
}

// What --accept-golden does when a golden is already there, and what this
// task exists to reach: the rename over an existing file is measured here on
// every platform the suite runs on, rather than assumed.
TEST_CASE("replaceFile replaces an existing file with exactly the new bytes") {
    const std::filesystem::path folder = scratchFolder("replace");
    const std::filesystem::path target = folder / "golden.png";
    // Longer than the new bytes, so that writing into the old file instead of
    // replacing it would leave its tail behind.
    std::vector<std::byte> old = everyByte();
    old.insert(old.end(), old.begin(), old.end());
    REQUIRE(writeFile(target, old));

    REQUIRE(replaceFile(target, bytesOf("the approved frame")));
    CHECK(contentsOf(target) == bytesOf("the approved frame"));
    CHECK_FALSE(std::filesystem::exists(partialOf(target)));

    std::filesystem::remove_all(folder);
}

TEST_CASE("replaceFile overwrites a partial file an earlier run left behind") {
    const std::filesystem::path folder = scratchFolder("leftover");
    const std::filesystem::path target = folder / "golden.png";
    REQUIRE(writeFile(target, bytesOf("the old golden")));
    REQUIRE(writeFile(partialOf(target), everyByte()));

    REQUIRE(replaceFile(target, bytesOf("the approved frame")));
    CHECK(contentsOf(target) == bytesOf("the approved frame"));
    CHECK_FALSE(std::filesystem::exists(partialOf(target)));

    std::filesystem::remove_all(folder);
}

TEST_CASE("a write that cannot start is reported, and leaves no file and no partial file") {
    const std::filesystem::path folder = scratchFolder("no_folder");
    const std::filesystem::path target = folder / "missing" / "golden.png";

    const auto replaced = replaceFile(target, everyByte());
    REQUIRE_FALSE(replaced);
    // Named by the file it could not open.
    CHECK(replaced.error().contains("golden.png.partial"));
    CHECK_FALSE(std::filesystem::exists(target));
    CHECK_FALSE(std::filesystem::exists(partialOf(target)));

    std::filesystem::remove_all(folder);
}

TEST_CASE("a rename that fails is reported, and leaves the target as it was and no partial file") {
    const std::filesystem::path folder = scratchFolder("rename_fails");
    // A folder, with a file in it, where the target should be: no platform
    // renames a file over a folder.
    const std::filesystem::path target = folder / "golden.png";
    std::filesystem::create_directories(target);
    REQUIRE(writeFile(target / "inside.txt", bytesOf("kept")));

    const auto replaced = replaceFile(target, everyByte());
    REQUIRE_FALSE(replaced);
    CHECK(replaced.error().contains("in place"));
    CHECK(replaced.error().contains("golden.png"));
    CHECK(std::filesystem::is_directory(target));
    CHECK(contentsOf(target / "inside.txt") == bytesOf("kept"));
    CHECK_FALSE(std::filesystem::exists(partialOf(target)));

    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}
