#include "core/Contract.hpp"

#ifndef NDEBUG
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stacktrace>
#include <string>

namespace orb::detail {

// Reported with std::fputs and every result discarded: this runs once the
// program is already wrong, inside a noexcept function, so a reporting path
// that can itself throw is not a reporting path, and if stderr is gone there
// is nobody left to tell -- the reasoning src/app/main.cpp gives for its own
// last words. Only the chain of calls needs memory to format, and a failure
// there costs the chain, never the abort.
//
// std::fputs is the C library's interface and takes an unbounded string, which
// is what -Wunsafe-buffer-usage-in-libc-call reports; off for this function
// alone, as src/app/main.cpp has it (ADR 0017).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
void contractViolated(Contract kind, ContractSite site, const std::stacktrace& calls) noexcept {
    const char* const what =
        kind == Contract::Precondition ? "precondition violated: " : "postcondition violated: ";
    static_cast<void>(std::fputs(what, stderr));
    static_cast<void>(std::fputs(site.condition, stderr));
    static_cast<void>(std::fputs("\n  in ", stderr));
    static_cast<void>(std::fputs(site.function, stderr));
    static_cast<void>(std::fputs(", ", stderr));
    static_cast<void>(std::fputs(site.file, stderr));
    static_cast<void>(std::fputs("\n  the chain of calls, the failing line first:\n", stderr));
    try {
        const std::string chain = std::to_string(calls);
        static_cast<void>(std::fputs(chain.c_str(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
    } catch (const std::exception&) {
        static_cast<void>(std::fputs("  (the chain of calls could not be formatted)\n", stderr));
    }
    static_cast<void>(std::fflush(stderr));
    std::abort();
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace orb::detail
#endif
