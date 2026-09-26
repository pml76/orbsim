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
#include "render/VulkanContext.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string_view>

struct SDL_Window;

namespace orb::app {

// What a probe run is asked for.
struct ProbeRequest {
    std::string_view probe;             // a name findProbe knows
    std::filesystem::path outDirectory; // created if it does not exist
    std::filesystem::path shaderDirectory;
    gfx::Validation validation{gfx::Validation::Disabled};
};

// Renders the probe and writes its files. Returns an exit code: 0 once every
// file is written, kExitFailure otherwise; main adds the validation verdict.
[[nodiscard]] int runProbe(SDL_Window* window,
                           const ProbeRequest& request,
                           std::atomic<std::uint32_t>& validationErrors);

// `--probe-list`: every probe's name and description, one to a line. Needs
// neither a window nor a device.
void listProbes();

} // namespace orb::app

#endif // ORBSIM_APP_PROBEMODE_HPP
