//
// libFuzzer entry point for the two-body core.
//
// VERIFICATION.md rule 13. The guidelines call fuzzing "a superb fit for this
// project" and underused by almost everybody, and the argument is specific:
// every orbital entry point takes six doubles and a gravitational parameter,
// and the interesting inputs are the degenerate ones nobody thought to write
// down. A fuzzer writes them down for you.
//
// What is asserted here is deliberately narrow, because the fuzzer does not
// know what the right answer is. It knows what must never happen:
//
//   * no crash, no undefined behaviour, no unbounded loop -- ASan and UBSan
//     are linked into this target and enforce those;
//   * a reported failure is a legitimate outcome for any input;
//   * but a *success* must not be NaN. Returning NaN elements while claiming
//     to have succeeded is the failure mode this exists to catch, because it
//     is the one that survives into a scenario file and reappears twenty
//     minutes into a flight.
//
// Infinity is not lumped in with NaN: `Elements::sma` is documented as
// infinite for a parabolic orbit and `OrbitInfo` returns infinite apoapsis and
// period for anything unbound. Those are answers, not failures.
//
// Build and run (Linux or WSL; clang's libFuzzer does not target the MSVC ABI):
//
//     cmake --preset linux-fuzz
//     cmake --build build/linux-fuzz
//     ./build/linux-fuzz/fuzz_orbit -max_total_time=60
//
#include "orbit/Orbit.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

using orb::Elements;
using orb::GravParam;
using orb::OrbitInfo;
using orb::Seconds;
using orb::StateVector;
using orb::Vec3;

// libFuzzer reports a non-zero exit as the finding, so abort is the whole
// vocabulary available for saying "this input broke an invariant".
void require(bool held) {
    if (!held) std::abort();
}

void requireNoNaN(const Vec3& v) {
    require(!std::isnan(v.x) && !std::isnan(v.y) && !std::isnan(v.z));
}

void checkElements(const Elements& el, GravParam mu) {
    for (const double value : std::array{el.sma.value,
                                         el.ecc.value,
                                         el.inc.value,
                                         el.lan.value,
                                         el.aop.value,
                                         el.tra.value,
                                         el.slr.value}) {
        require(!std::isnan(value));
    }

    // mu > 0 is already established: elementsFromState reports
    // NonPositiveGravity rather than returning a value otherwise, so
    // orbitInfo's precondition holds here.
    const OrbitInfo info = orbitInfo(el, mu);
    for (const double value : std::array{info.periapsis.value,
                                         info.apoapsis.value,
                                         info.period.value,
                                         info.meanMotion.value,
                                         info.energy.value,
                                         info.radius.value,
                                         info.speed.value}) {
        require(!std::isnan(value));
    }
}

// Seven doubles: position, velocity, and mu. Anything shorter is not a case.
constexpr std::size_t kDoublesNeeded = 7;
constexpr std::size_t kBytesNeeded = kDoublesNeeded * sizeof(double);

} // namespace

// The name and signature are libFuzzer's, not this codebase's, so the naming
// check cannot apply to it. libFuzzer declares it in its own driver, in no
// header this file can include, which is what -Wmissing-prototypes reports; it
// is off for this definition (ADR 0017).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < kBytesNeeded) return 0;

    // memcpy rather than a cast: the bytes are not aligned for a double, and
    // type-punning through a reinterpret_cast is undefined -- which UBSan would
    // rightly report as a bug in this harness rather than in the code under it.
    // libFuzzer hands the input over as a pointer and a size, and the size is
    // checked above; a C library copy from a bare pointer is what
    // -Wunsafe-buffer-usage-in-libc-call reports, and it is off for this one
    // (ADR 0017).
    std::array<double, kDoublesNeeded> raw{};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
    std::memcpy(raw.data(), data, kBytesNeeded);
#pragma clang diagnostic pop

    const StateVector sv{.pos = {raw[0], raw[1], raw[2]}, .vel = {raw[3], raw[4], raw[5]}};
    const GravParam mu{raw[6]};

    // The time step reuses an input word so the fuzzer can steer it too.
    const Seconds dt{raw[3]};

    if (const auto el = elementsFromState(sv, mu)) checkElements(*el, mu);

    if (const auto moved = propagate(sv, mu, dt)) {
        requireNoNaN(moved->pos);
        requireNoNaN(moved->vel);
    }

    return 0;
}
#pragma clang diagnostic pop
