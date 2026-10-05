#ifndef ORBSIM_VIEW_FILEWRITE_HPP
#define ORBSIM_VIEW_FILEWRITE_HPP
//
// Writing a file's bytes, and replacing a file without ever leaving half of
// one (M1-108; register decisions 232 and 387).
//
// **Here rather than in src/app**, because the rule `--accept-golden` relies
// on -- write beside the file, then rename over it -- sat inside the
// application where no test could reach it, and no test may run
// --accept-golden itself (decision 233). In orbsim_view,
// tests/test_file_replace.cpp holds it on a temporary folder instead
// (ADR 0012). writeFile moved with it from src/app/ProbeMode.cpp, rather than
// being copied, since replaceFile needs it.
//
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace orb::view {

// gcc's -Wabi-tag: std::string carries libstdc++'s "cxx11" ABI tag, and gcc
// wants everything returning one to carry it too. The tag guards code shipped
// as a binary against the old string ABI; this project builds everything from
// source with one ABI. Off at this site alone, for gcc alone -- register
// decision 52's ruling and shape.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif

// Writes bytes to a file, replacing what was there, through
// std::filesystem::path so that the platform's own encoding of the name is
// used (CODING_GUIDELINES section 18). On failure, says why, naming the file;
// what was written before the failure may be left behind.
[[nodiscard]] std::expected<void, std::string> writeFile(const std::filesystem::path& path,
                                                         std::span<const std::byte> bytes);

// Puts bytes in place as `target` in one step: they are written to
// `<target>.partial` beside it, so that the rename stays on one volume, and
// that file is then renamed over `target`. So a failed write never leaves
// half of a file where `target` was, and `target` is either what it was or
// all of `bytes` (decision 232). On failure the partial file is removed and
// the reason returned; a `.partial` left by an earlier run is overwritten.
// The folder `target` is in must exist.
[[nodiscard]] std::expected<void, std::string> replaceFile(const std::filesystem::path& target,
                                                           std::span<const std::byte> bytes);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::view

#endif // ORBSIM_VIEW_FILEWRITE_HPP
