//
// The program scripts/check-reproducible-build.py compiles and links a second
// time, requiring the same bytes (M1-129, register decisions 488 and 504).
//
// What it computes does not matter; that it is a real program does. It calls
// one function the core library defines out of line, so its link reads a
// library and not only its own object -- a program of the same shape as every
// suite. No test runs it, and nothing else may: the check overwrites it while
// other tests run, and Windows refuses to overwrite a program that is running.
//
#include "core/LeapSeconds.hpp"

int main() {
    const orb::TaiLookup first = orb::leapSecondLookupForTai(orb::kLeapSecondEraFirstTai);
    // 10 s, the offset UTC started the era with (core/LeapSeconds.hpp).
    return first.deltaAtSeconds == 10 ? 0 : 1;
}
