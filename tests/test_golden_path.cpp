//
// Tests for view/GoldenPath.hpp: where a graphics card's golden images are
// (M1-110; register decisions 287, 289 and 294).
//
// **Against strings written here by hand**, never against a std::format call
// like the one the code makes. The three real cards are the numbers their
// probes' sidecars printed -- 0x1002 0x744c on the second machine, 0x10de
// 0x25ba and 0x8086 0x4626 in docs/measurements/m1-19-line-raster/ -- and
// every other case is chosen to break one way of getting the name wrong:
// the two numbers swapped, uppercase digits, a short number's leading zeros
// dropped, a long number cut to four digits.
//
#include "view/GoldenPath.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace orb::view;

TEST_CASE("a card's folder is named by its vendor and device numbers, in that order") {
    CHECK(goldenFolderName({.vendorId = 0x1002, .deviceId = 0x744c}) == "1002-744c");
    CHECK(goldenFolderName({.vendorId = 0x10de, .deviceId = 0x25ba}) == "10de-25ba");
    CHECK(goldenFolderName({.vendorId = 0x8086, .deviceId = 0x4626}) == "8086-4626");
    // The same two numbers the other way round name a different folder.
    CHECK(goldenFolderName({.vendorId = 0x744c, .deviceId = 0x1002}) == "744c-1002");
}

TEST_CASE("a card's folder name is in lowercase hexadecimal") {
    CHECK(goldenFolderName({.vendorId = 0xabcd, .deviceId = 0xef01}) == "abcd-ef01");
    // Decimal would read 4098-29772 for the second machine's card.
    CHECK(goldenFolderName({.vendorId = 0x1002, .deviceId = 0x744c}) != "4098-29772");
}

TEST_CASE("a short number keeps its leading zeros") {
    CHECK(goldenFolderName({.vendorId = 0x00a1, .deviceId = 0x0007}) == "00a1-0007");
    CHECK(goldenFolderName({.vendorId = 0x0000, .deviceId = 0x0000}) == "0000-0000");
}

TEST_CASE("a number wider than four digits is written in full") {
    // VK_VENDOR_ID_MESA, a vendor number Khronos assigned rather than PCI.
    CHECK(goldenFolderName({.vendorId = 0x10005, .deviceId = 0x0000}) == "10005-0000");
    CHECK(goldenFolderName({.vendorId = 0xffffffff, .deviceId = 0x12345678}) ==
          "ffffffff-12345678");
}

TEST_CASE("a probe's golden is <directory>/<card>/<probe>.png") {
    const std::filesystem::path clear =
        goldenPathFor("tests/golden", {.vendorId = 0x1002, .deviceId = 0x744c}, "clear");
    CHECK(clear.generic_string() == "tests/golden/1002-744c/clear.png");

    const std::filesystem::path halfAu = goldenPathFor("C:/GitHub/orbsim/tests/golden",
                                                       {.vendorId = 0x10de, .deviceId = 0x25ba},
                                                       "lambert-half-au");
    CHECK(halfAu.generic_string() == "C:/GitHub/orbsim/tests/golden/10de-25ba/lambert-half-au.png");
}
