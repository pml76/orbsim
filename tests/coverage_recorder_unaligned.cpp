//
// Flags without their addresses, for the coverage recorder's self-test (M1-131,
// register decisions 559, 627 and 632).
//
// Compiled with the hook less its address table -- CMakeLists.txt adds
// `-fno-sanitize-coverage=pc-table` to this file alone -- and linked into
// `coverage_recorder_unaligned` beside tests/coverage_recorder_probe.cpp, so that
// the program holds one flag more than its table has entries. The recorder must
// then write the flags' count "out of line" and no function at all: a flag it
// cannot place beside an address names no function, and every one after it
// would name the wrong one. scripts/check-coverage-hook.py holds this object to
// carrying the flags and not the table.
//
// The function is never called. It has C linkage so that it keeps the
// external linkage a function needs to be compiled at all when nothing in its
// file calls it, without a header of its own, which `misc-use-internal-linkage`
// would otherwise ask for; the program is linked with debug information, so
// lld-link keeps it (`/OPT:NOREF`).
//

extern "C" int coverageFlagWithoutAnAddress(int count);

extern "C" int coverageFlagWithoutAnAddress(int count) { return count + 1; }
