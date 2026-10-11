// **This file must not compile.** It is the guard for -Wfloat-equal after
// core/Units.hpp (M1-117, register decisions 451 and 454), and
// scripts/check-float-equal-guard.py compiles it with the build tree's own
// flags as the CTest test `float_equal_guard`, which passes only when the one
// error is -Wfloat-equal, here.
//
// mp-units v2.5.0 left that warning switched off for the rest of every file
// that includes it, and eight comparisons like the one below went unseen.
// core/Units.hpp now undoes the stray pushes; if that ever stops working, this
// file compiles and the test fails. core/Units.hpp is included for that alone:
// nothing below uses it, and that is the point.
//
// Two named constants rather than literals, because clang exempts a comparison
// against a literal it holds exactly, and gcc does not; a comparison of two
// variables is reported by both. With ORBSIM_FLOAT_EQUAL_GUARD_CONTROL defined
// the comparison is the one this project writes instead, and the file must
// compile clean: that is what pins the failure on the comparison rather than
// on anything else here.
#include "core/Scalar.hpp"
#include "core/Units.hpp"

namespace orb {

constexpr f64 kOneTenth = 0.1;
constexpr f64 kSameTenth = 0.1;

#ifdef ORBSIM_FLOAT_EQUAL_GUARD_CONTROL
static_assert(bitsOf(kOneTenth) == bitsOf(kSameTenth));
#else
static_assert(kOneTenth == kSameTenth);
#endif

} // namespace orb
