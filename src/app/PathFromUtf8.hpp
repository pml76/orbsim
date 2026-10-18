#ifndef ORBSIM_APP_PATHFROMUTF8_HPP
#define ORBSIM_APP_PATHFROMUTF8_HPP
//
// A path from UTF-8 text, converted in one place (M1-131, register decision
// 659): src/app/main.cpp's arguments and built-in folders, and the shader
// record's folder, src/app/ShaderRecord.cpp.
//
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace orb::app {

// A path from the UTF-8 text SDL hands main its arguments in.
//
// **SDL's arguments are UTF-8 on every platform**: on Windows, where SDL
// supplies the entry point, it re-reads the wide command line and converts it
// (WIN_CheckDefaultArgcArgv in SDL's src/core/windows/SDL_windows.c). A
// std::filesystem::path built from a char string reads it in the *current
// code page* on Windows instead, which is the same text only while every
// character is ASCII -- the failure CODING_GUIDELINES section 18 describes,
// for a user whose account name steps outside it. Relabelling the bytes as
// char8_t is what tells the path they are UTF-8. ORBSIM_SHADER_DIR goes through
// here too: a string literal in this project's sources is UTF-8. So does the
// shader record's folder, which SDL_getenv returns in UTF-8 (decision 644).
[[nodiscard]] inline std::filesystem::path pathFromUtf8(std::string_view text) {
    std::u8string utf8(text.size(), u8'\0');
    std::ranges::transform(
        text, utf8.begin(), [](char unit) { return static_cast<char8_t>(unit); });
    return std::filesystem::path{utf8};
}

} // namespace orb::app

#endif // ORBSIM_APP_PATHFROMUTF8_HPP
