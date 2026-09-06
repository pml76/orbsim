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
#include <cassert>

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
// suppression sits here, on the two definitions, rather than in .clang-tidy
// where it would silently cover the next macro somebody adds.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORBSIM_EXPECTS(condition) assert((condition) && "precondition violated")
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ORBSIM_ENSURES(condition) assert((condition) && "postcondition violated")

#endif // ORBSIM_CORE_CONTRACT_HPP
