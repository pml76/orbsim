#include "view/GoldenPath.hpp" // SF.5: own header, first

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace orb::view {

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
std::string goldenFolderName(CardId card) {
    // {:04x}: lowercase hexadecimal, padded with zeros to four digits and
    // never cut, so a number of five digits or more is written in full.
    return std::format("{:04x}-{:04x}", card.vendorId, card.deviceId);
}

std::filesystem::path
goldenPathFor(const std::filesystem::path& directory, CardId card, std::string_view probe) {
    // Appended to the name rather than set with replace_extension, which
    // would cut a probe name at a dot of its own.
    return directory / goldenFolderName(card) / (std::string(probe) + ".png");
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::view
