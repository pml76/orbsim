#ifndef ORBSIM_VIEW_GOLDENPATH_HPP
#define ORBSIM_VIEW_GOLDENPATH_HPP
//
// Where a graphics card's golden images are (M1-110; ADR 0008's update of
// 2026-10-03; register decisions 287, 289 and 294).
//
// **Each card has its own goldens**, in a folder named after the Vulkan
// vendor and device numbers it reports: `tests/golden/1002-744c/clear.png` is
// `clear` as approved on an AMD Radeon RX 7900 XTX. Not the driver version,
// on purpose: a driver update that moves a picture fails and is looked at,
// rather than finding no golden and being approved again without a look
// (decision 287). Two cards draw a line one pixel wide on different pixels
// (decision 278), which is why one golden for every card no longer holds.
//
// **Headless and pure**, which is why it is here and not in src/app: the
// application resolves the path from the device it opened, and
// tests/test_golden_path.cpp holds the name to hand-written strings without a
// GPU (ADR 0012).
//
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace orb::view {

// The two numbers that identify a graphics card to Vulkan, as
// VkPhysicalDeviceProperties reports them. A struct, so that the two
// same-typed numbers are named at every construction site and cannot be
// transposed (non-negotiable 1; decision 294).
struct CardId {
    std::uint32_t vendorId{};
    std::uint32_t deviceId{};
};

// gcc's -Wabi-tag: std::string and std::filesystem::path carry libstdc++'s
// "cxx11" ABI tag, and gcc wants everything returning one to carry it too.
// The tag guards code shipped as a binary against the old string ABI; this
// project builds everything from source with one ABI. Off at this site alone,
// for gcc alone -- register decision 52's ruling and shape.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif

// The card's folder name: vendor, a hyphen, device, each in lowercase
// hexadecimal with at least four digits -- "1002-744c". A PCI number is four
// digits, so the leading zeros of a small one are kept; a vendor number
// Khronos assigned to a maker without a PCI number (0x10000 and up) is
// written in full, five digits or more. The same digits the probe's sidecar
// prints after "0x" for gpu.vendor and gpu.device.
[[nodiscard]] std::string goldenFolderName(CardId card);

// `<directory>/<goldenFolderName(card)>/<probe>.png`: where a probe's golden
// is for this card, whether it exists or not.
[[nodiscard]] std::filesystem::path
goldenPathFor(const std::filesystem::path& directory, CardId card, std::string_view probe);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::view

#endif // ORBSIM_VIEW_GOLDENPATH_HPP
