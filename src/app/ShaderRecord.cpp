#include "app/ShaderRecord.hpp" // SF.5: own header, first
#include "app/PathFromUtf8.hpp"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <ios>
#include <ostream>
#include <process.h>
#include <string>
#include <string_view>
#include <utility>

namespace orb::app {

namespace {

// Empty where the variable is not set. Through SDL_getenv rather than
// std::getenv, which the Windows SDK marks deprecated -- an error under
// -Werror -- and concurrency-mt-unsafe reports; SDL's is documented as
// thread-safe, and returns UTF-8 on every platform (decision 644).
[[nodiscard]] std::string_view environmentValue(const char* name) {
    const char* value = SDL_getenv(name);
    return value == nullptr ? std::string_view{} : std::string_view{value};
}

// A path's UTF-8 text, the bytes relabelled as char for the stream.
[[nodiscard]] std::string utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    std::string utf8(text.size(), '\0');
    std::ranges::transform(
        text, utf8.begin(), [](char8_t unit) noexcept { return static_cast<char>(unit); });
    return utf8;
}

} // namespace

ShaderRecord ShaderRecord::open() {
    const std::string_view folder = environmentValue("ORBSIM_COVERAGE_RECORDS");
    if (folder.empty()) return ShaderRecord{};
    // Named after the process number and the time it was opened, as the
    // coverage recorder's are, since Windows reuses process numbers
    // (decisions 564 and 632). The number is the C runtime's: neither the
    // standard library nor SDL has one for the running process.
    const auto started = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return ShaderRecord{pathFromUtf8(folder) /
                        std::format("{}-{}.shaders.txt", _getpid(), started.count())};
}

ShaderRecord::ShaderRecord(const std::filesystem::path& file) : file_(file, std::ios::binary) {
    writeLine(std::format("test {}", environmentValue("ORBSIM_TEST_NAME")));
}

void ShaderRecord::writeShader(const std::filesystem::path& shader) {
    if (!file_.is_open()) return;
    std::string line = utf8Of(shader);
    if (std::ranges::find(written_, line) != written_.end()) return;
    writeLine(line);
    written_.push_back(std::move(line));
}

void ShaderRecord::writeEnd() { writeLine("end"); }

void ShaderRecord::writeLine(std::string_view line) {
    if (!file_.is_open()) return;
    // Binary, so that a line ends in '\n' alone; flushed, so that the line
    // has left the program before it goes on (decision 629).
    file_ << line << '\n' << std::flush;
}

} // namespace orb::app
