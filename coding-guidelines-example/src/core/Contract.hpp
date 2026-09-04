#pragma once
//
// Preconditions and postconditions, until C++26 contracts arrive.
//
// [S2] The Core Guidelines call these Expects() and Ensures() (I.6, I.8). The
// names matter far less than the habit of writing the requirement down in a
// place the compiler, the debugger and the next reader can all see it.
//
// [S20] JPL rule 5 asks for assertions on the things that "cannot happen".
// These are those. Conditions that *can* happen -- user input, a malformed
// scenario file -- are reported through std::expected instead and are never
// asserted. The line between the two is drawn in docs/adr/0002.
//
#include <cassert>

// The condition must be free of side effects.
//
// Assertions vanish under NDEBUG and take any side effect inside them with it,
// which produces a bug that exists only in the configuration you ship. If you
// need the result, compute it first and assert on the variable:
//
//     [[maybe_unused]] const auto solved = solveKepler(anomaly, ecc);
//     ORBEX_EXPECTS(solved.has_value());
//
// [S15] ALL_CAPS is correct here and nowhere else: NL.9 reserves it for macros
// exactly so a reader can tell a macro from a function at the call site.
#define ORBEX_EXPECTS(condition) assert((condition) && "precondition violated")
#define ORBEX_ENSURES(condition) assert((condition) && "postcondition violated")
