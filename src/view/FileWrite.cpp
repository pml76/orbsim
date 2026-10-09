#include "view/FileWrite.hpp" // SF.5: own header, first

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace orb::view {

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif

namespace {

// What writeFile and writeText share: the stream, its mode and its checks.
[[nodiscard]] std::expected<void, std::string> writeChars(const std::filesystem::path& path,
                                                          std::span<const char> chars) {
    // Binary alone: an output stream truncates by itself, and openmode is a
    // signed bitmask type, so combining two of its values is what
    // bugprone-signed-bitwise reports.
    std::ofstream file(path, std::ios::binary);
    if (!file) return std::unexpected("cannot open " + path.string() + " for writing");
    file.write(chars.data(), static_cast<std::streamsize>(chars.size()));
    // Closed and checked here, not left to the destructor: the last bytes stay
    // in the stream's buffer until the close, so a disk that refuses them
    // refuses only then, and a destructor reports nothing. Measured with
    // /dev/full (M1-111): 4,096 bytes were reported written without this.
    file.close();
    if (!file) return std::unexpected("could not write all of " + path.string());
    return {};
}

} // namespace

std::expected<void, std::string> writeFile(const std::filesystem::path& path,
                                           std::span<const std::byte> bytes) {
    // The bytes become chars by value rather than by a pointer cast, which
    // costs one copy of a few megabytes.
    std::vector<char> chars(bytes.size());
    std::ranges::transform(
        bytes, chars.begin(), [](std::byte b) noexcept { return static_cast<char>(b); });
    return writeChars(path, chars);
}

std::expected<void, std::string> writeText(const std::filesystem::path& path,
                                           std::string_view text) {
    return writeChars(path, text);
}

std::expected<void, std::string> replaceFile(const std::filesystem::path& target,
                                             std::span<const std::byte> bytes) {
    std::filesystem::path partial = target;
    partial += ".partial";
    std::error_code error;
    if (auto written = writeFile(partial, bytes); !written) {
        std::filesystem::remove(partial, error);
        return written;
    }
    std::filesystem::rename(partial, target, error);
    if (error) {
        // The reason is formatted before the removal reuses `error`.
        std::string why =
            std::format("cannot put {} in place: {}", target.string(), error.message());
        std::filesystem::remove(partial, error);
        return std::unexpected(std::move(why));
    }
    return {};
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::view
