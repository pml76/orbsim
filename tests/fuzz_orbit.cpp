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
// Build and run, on Windows, where this now runs (2026-09-12):
//
//     cmake --preset windows-fuzz
//     cmake --build build/windows-fuzz
//     build\windows-fuzz\fuzz_orbit.exe -max_total_time=60
//
// No PATH to set: the preset puts clang's ASan DLL beside the executable,
// without which it does not reach main. It used to say here that clang's
// libFuzzer does not target the MSVC ABI; that was false, and the recipe it
// really needs is in CMakeLists.txt beside the option.
//
// The same target still builds under WSL with `--preset linux-fuzz`, and is
// kept for one reason: LeakSanitizer does not exist on Windows. Nothing on the
// path fuzzed here allocates, so it has nothing to find today.
//
// Every tree also compiles this file without libFuzzer, into
// orbsim_fuzz_objects, so that the warnings and check's lint cover it even
// where the fuzzer itself is not built.
//
#include "core/Math.hpp"
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

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

template <auto R> void requireNoNaN(const Vec3<R>& v) {
    require(!std::isnan(v.x.value()) && !std::isnan(v.y.value()) && !std::isnan(v.z.value()));
}

void checkElements(const Elements& el, GravParam mu) {
    // std::to_array rather than a braced std::array, which leans on brace
    // elision -- what gcc's -Wmissing-braces reports.
    for (const double value : std::to_array<double>({
             el.sma.value(),
             el.ecc.value(),
             el.inc.value(),
             el.lan.value(),
             el.aop.value(),
             el.tra.value(),
             el.slr.value(),
         })) {
        require(!std::isnan(value));
    }

    // The reverse conversion, over exactly the element sets the forward one
    // produces. Free to add and it would have caught the defect of 2026-09-13
    // straight away: stateFromElements returned a NaN position for 1.7% of
    // nearly radial element sets, and nothing here was asking it anything.
    if (const auto back = stateFromElements(el, mu)) {
        requireNoNaN(back->pos);
        requireNoNaN(back->vel);
    }

    // orbitInfo has no precondition left to hold: GravParam carries mu > 0
    // itself since M1-87, which is what let the assertion here go.
    const OrbitInfo info = orbitInfo(el, mu);
    for (const double value : std::to_array<double>({
             info.periapsis.value(),
             info.apoapsis.value(),
             info.period.value(),
             info.meanMotion.value(),
             info.energy.value(),
             info.radius.value(),
             info.speed.value(),
         })) {
        require(!std::isnan(value));
    }
}

// Seven doubles: position, velocity, and mu. Anything shorter is not a case.
constexpr std::size_t kDoublesNeeded = 7;
constexpr std::size_t kBytesNeeded = kDoublesNeeded * sizeof(double);

} // namespace

// The entry point, declared as libFuzzer's own driver declares it. libFuzzer
// ships no header for it, and a definition with no declaration before it is
// what clang's -Wmissing-prototypes and gcc's -Wmissing-declarations report;
// declaring it answers both rather than silencing either. The name and
// signature are libFuzzer's, not this codebase's, so the naming check cannot
// apply to them, and clang-tidy reports the name here, at its first
// declaration.
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < kBytesNeeded) return 0;

    // memcpy rather than a cast: the bytes are not aligned for a double, and
    // type-punning through a reinterpret_cast is undefined -- which UBSan would
    // rightly report as a bug in this harness rather than in the code under it.
    // libFuzzer hands the input over as a pointer and a size, and the size is
    // checked above; a C library copy from a bare pointer is what clang's
    // -Wunsafe-buffer-usage-in-libc-call reports, and it is off for this one
    // (ADR 0017) -- for clang alone, since gcc has no such warning and reports
    // a pragma it does not recognise.
    std::array<double, kDoublesNeeded> raw{};
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
    std::memcpy(raw.data(), data, kBytesNeeded);
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    // Named rather than indexed: the binding has exactly as many names as the
    // array has elements, which the compiler checks, so no read can fall
    // outside it.
    const auto [posX, posY, posZ, velX, velY, velZ, muValue] = raw;
    const StateVector sv{.pos = {posX, posY, posZ}, .vel = {velX, velY, velZ}};

    // **The factory is fuzzed, and the case is not thrown away for failing it.**
    //
    // Since M1-87 a GravParam cannot hold a value that is not finite and
    // positive, so the raw word goes to the factory and its verdict is a claim:
    // whatever comes back accepted must be inside the range it promises. That
    // is what fuzz_time already does for DeltaUt1 and DeltaT.
    const auto rawMu = GravParam::from(muValue);
    if (rawMu) {
        require(std::isfinite(rawMu->value()));
        require(rawMu->value() > 0.0);
    }

    // Then the case continues on a mu the bytes can always produce. **The first
    // version of this simply returned when the factory refused, and that threw
    // away 50.02% of the input space** -- every negative, zero or non-finite mu
    // word, and with it the six state-vector words of the same input, which had
    // nothing wrong with them. Measured over the 64-bit pattern space on
    // 2026-09-22, which is how it was noticed at all.
    //
    // Folding the sign away keeps the magnitude the fuzzer chose and costs only
    // the sign bit, which nothing downstream can use: mu <= 0 is unrepresentable
    // now, so there is no behaviour left for a negative word to reach. Only a
    // NaN or an infinity dead-ends here, about 0.02% of patterns.
    const auto usable =
        GravParam::from(std::fabs(muValue) + std::numeric_limits<double>::denorm_min());
    if (!usable) return 0;
    const GravParam mu = *usable;

    // The time step reuses an input word so the fuzzer can steer it too.
    const Seconds dt{velX};

    if (const auto el = elementsFromState(sv, mu)) checkElements(*el, mu);

    if (const auto moved = propagate(sv, mu, dt)) {
        requireNoNaN(moved->pos);
        requireNoNaN(moved->vel);
    }

    return 0;
}
