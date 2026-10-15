#ifndef ORBSIM_CORE_CONTRACT_HPP
#define ORBSIM_CORE_CONTRACT_HPP
//
// Preconditions and postconditions, until C++26 contracts arrive.
//
// The Core Guidelines call these Expects() and Ensures() (I.6, I.8). The names
// matter far less than the habit of writing the requirement down somewhere the
// compiler, the debugger and the next reader can all see it.
//
// Where the line falls, and why (CODING_GUIDELINES.md sections 2, 7 and 20):
//
//   * A condition a caller can legitimately produce -- an eccentricity from a
//     scenario file, a malformed element set -- is REPORTED, by returning
//     std::expected. It is user input, not a programmer error.
//   * A condition that cannot happen unless this code is wrong is ASSERTED.
//     That a wrapped anomaly lies in (-pi, pi] is not a runtime possibility;
//     it is a claim about the function that just produced it.
//
// JPL's Power of Ten asks for assertions on the things that "cannot happen"
// (rule 5), and pairs that with bounding every loop (rule 2). A bounded loop
// that stays silent when it fails to converge has done only half the job.
//
// The condition must be free of side effects.
//
// Assertions vanish under NDEBUG and take anything inside them along, which
// produces a bug that exists only in the configuration you ship. If you need
// the result, compute it first and assert on the variable:
//
//     [[maybe_unused]] const auto solved = solveKepler(meanAnomaly, ecc);
//     ORBSIM_EXPECTS(solved.has_value());
//
// ALL_CAPS is correct here and nowhere else in this codebase: NL.9 reserves it
// for macros, precisely so a reader can tell a macro from a function on sight.
//
// These two are the only macros this codebase permits, and they have to be
// macros: a function cannot capture the source text of its own argument, which
// is the whole value of an assertion message, and it would evaluate the
// condition even under NDEBUG. C++26 contracts replace them; until then the
// suppression sits here, on their definitions -- one with assertions live and
// one without, for each -- rather than in .clang-tidy where it would silently
// cover the next macro somebody adds.
//
// **What a failure says, and what it does not carry** (M1-129, register
// decisions 549, 556 and 557). It names the condition, the function and the
// file -- none of which moves when a line is added above the assertion -- and
// prints the chain of calls, which reads the line from the debug information.
// The line is not a number in the compiled code, as `assert`'s is: that number
// changes the code of every function below an edit, and the test selection
// reruns a test whenever a function it entered changed. The chain of calls is
// taken here, at the failing line, and not in the handler: taken there, an
// unoptimised build names the line after the assertion (measured 2026-10-15).
// Where a tree has no debug information -- MSVC's links without it, register
// decision 511 -- the chain names only the program and an offset, and the
// condition, function and file stand alone. "precondition violated" stays in
// the message: cmake/VerifyAssertionProbe.cmake requires it of an abort.
#ifdef NDEBUG
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORBSIM_EXPECTS(expression) static_cast<void>(0)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORBSIM_ENSURES(expression) static_cast<void>(0)
#else
#include <stacktrace>

namespace orb::detail {

enum class Contract : unsigned char { Precondition, Postcondition };

// Where a contract failed, each text named at the call so that none can be
// passed in another's place (bugprone-easily-swappable-parameters).
struct ContractSite {
    const char* condition;
    const char* file;
    const char* function;
};

// Writes what failed and the chain of calls to stderr, then aborts.
[[noreturn]] void
contractViolated(Contract kind, ContractSite site, const std::stacktrace& calls) noexcept;

} // namespace orb::detail

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORBSIM_EXPECTS(expression)                                                                 \
    ((expression) ? static_cast<void>(0)                                                           \
                  : ::orb::detail::contractViolated(                                               \
                        ::orb::detail::Contract::Precondition,                                     \
                        {.condition = #expression, .file = __FILE__, .function = __func__},        \
                        std::stacktrace::current()))
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORBSIM_ENSURES(expression)                                                                 \
    ((expression) ? static_cast<void>(0)                                                           \
                  : ::orb::detail::contractViolated(                                               \
                        ::orb::detail::Contract::Postcondition,                                    \
                        {.condition = #expression, .file = __FILE__, .function = __func__},        \
                        std::stacktrace::current()))
#endif

#endif // ORBSIM_CORE_CONTRACT_HPP
