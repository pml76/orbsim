//
// Provoke a count subtraction that would wrap, which must abort
// (M1-12, decision 136). Everything is in tests/CountWraparound.hpp; each of
// the three operations needs a process of its own, because a program can only
// abort once.
//
#include "tests/CountWraparound.hpp"

int main(int argc, char** argv) {
    static_cast<void>(argv);
    return orb::probe::runProbe(orb::probe::Operation::Subtract, argc);
}
