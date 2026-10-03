#ifndef ORBSIM_APP_PROBEMODE_HPP
#define ORBSIM_APP_PROBEMODE_HPP
//
// `--probe <name>`: render one probe's frame and write what it produced
// (M1-16; ADR 0008; register decisions 187-194).
//
// **Five files, always, and before any verdict** (decision 193): the HDR
// dump, the two PNGs and the EXR once a frame exists, and the sidecar in
// every case -- a run that could not render writes the sidecar alone, with
// the reason in it. The validation layers' count is read only after the
// renderer has been torn down, by main, so it decides the exit code but
// cannot stop a file being written.
//
// **`--golden <path>`** (M1-17; ADR 0008; register decisions 228-237): once
// the files are written, the 8-bit frame is halved to 640x360 in linear light
// and held against the golden. A mismatch writes `<name>.diff.png` and ends in
// exit 4; a golden that cannot be read ends in exit 1, with the reason.
// **`--accept-golden`** compares too, if a golden is there, and hands the
// halved frame back to main, which writes it only once the renderer is torn
// down and the validation count is known to be zero (decision 232).
//
// **`--golden-dir <dir>`** (M1-110; register decisions 287-299): the same,
// with the golden found from the card the device was opened on --
// `<dir>/<vendor>-<device>/<probe>.png`, view/GoldenPath.hpp. A card with no
// golden there fails with exit 1, naming the card and the command that
// approves one; it is never a comparison skipped (VERIFICATION.md rule 23).
// `--accept-golden` with it writes there, creating the card's folder.
//
#include "render/VulkanContext.hpp"
#include "view/ImageCompare.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

struct SDL_Window;

namespace orb::app {

// What a probe run does with a golden image. An enum rather than two
// booleans, which could both be set (non-negotiable 2).
enum class GoldenAction : std::uint8_t {
    None,    // no --golden
    Compare, // --golden <path>
    Accept,  // --golden <path> --accept-golden
};

// How the golden was named: as one file, or as a directory holding one folder
// per graphics card (decision 289). An enum, for the reason GoldenAction is.
enum class GoldenLocation : std::uint8_t {
    File,        // --golden <path>: goldenPath is the golden
    CardFolders, // --golden-dir <dir>: goldenPath is the directory of card folders
};

// What a probe run is asked for.
struct ProbeRequest {
    std::string_view probe;             // a name findProbe knows
    std::filesystem::path outDirectory; // created if it does not exist
    std::filesystem::path shaderDirectory;
    gfx::Validation validation{gfx::Validation::Disabled};
    GoldenAction golden{GoldenAction::None};
    std::filesystem::path goldenPath; // empty unless golden is Compare or Accept
    GoldenLocation goldenLocation{GoldenLocation::File};
};

// A frame to be written as a golden, and the file it goes to -- together,
// because under --golden-dir only the device could say which file that is.
struct GoldenToAccept {
    view::Rgb8Image frame;
    std::filesystem::path file;
};

// What a probe run leaves for main: an exit code before the validation
// verdict -- 0, kExitFailure or kExitGoldenMismatch -- and, when a golden is
// to be accepted and every file was written, the halved frame to write.
struct ProbeOutcome {
    int exitCode{};
    std::optional<GoldenToAccept> toAccept;
};

// Renders the probe, writes its files and compares its frame with the golden,
// if one was named. main adds the validation verdict (decision 230).
[[nodiscard]] ProbeOutcome runProbe(SDL_Window* window,
                                    const ProbeRequest& request,
                                    std::atomic<std::uint32_t>& validationErrors);

// Writes the frame to its file as the new approved frame, creating the
// folder it goes in: to a temporary file beside it, then renamed over it, so
// a failed write never leaves half a golden (decision 232). Returns 0, or
// kExitFailure with the reason printed.
[[nodiscard]] int acceptGolden(const GoldenToAccept& accepted);

// `--probe-list`: every probe's name and description, one to a line. Needs
// neither a window nor a device.
void listProbes();

} // namespace orb::app

#endif // ORBSIM_APP_PROBEMODE_HPP
