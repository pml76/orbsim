# The test suites, each with the abort listener linked in.
#
# Moved out of CMakeLists.txt unchanged by M1-105 (register decision 254), and
# included where it stood, in the same directory scope: every variable and
# target it uses or makes is the top-level file's, as before. It has a file of
# its own so that the mutants aimed at it -- scripts/mutants/m1-90.json and
# m1-95.json -- are due again when this file changes, and not on every edit to
# CMakeLists.txt. Registered with the configure guard (M1-98) before it is
# included.

# tests/AbortBehaviour.cpp is linked into every suite as an OBJECT library, not
# from orbsim_test_support, and the difference matters: a Catch2 listener in a
# static library is only pulled in if something already references that library,
# and test_render_quality references none of the support code. It would have
# linked clean and silently lost the behaviour. An OBJECT library has no such
# choice -- its object is linked into each program directly -- so the listener
# is compiled once and linted once, where until 2026-09-27 it was compiled into
# each of 23 programs and linted 23 times, the slowest lint step in every tree
# (M1-90, ADR 0024, register decision 210). The CTest test abort_listener asks
# every suite, through Catch2's --list-listeners, whether it has it. See the
# file for what it is for.
add_library(orbsim_abort_behaviour OBJECT tests/AbortBehaviour.cpp)
target_link_libraries(orbsim_abort_behaviour PRIVATE orbsim_test_support)
foreach(test_name IN LISTS ORBSIM_TESTS)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE orbsim_abort_behaviour orbsim_test_support)
    # One CTest test per TEST_CASE, so `ctest -R` can select a single case and
    # a failure names the case rather than the executable.
    catch_discover_tests(${test_name})
endforeach()
