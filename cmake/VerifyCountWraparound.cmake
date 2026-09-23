# Run the count-wraparound probe and decide which way round its exit code means.
#
#     cmake -DPROBE=<path to count_wraparound_probe> -P cmake/VerifyCountWraparound.cmake
#
# Run by CTest as `count_wraparound_aborts` (M1-12, register decision 136).
# The probe provokes `Texels{1} - Texels{2}` at run time, which core/Scalar.hpp
# asserts against, so **the probe failing is the pass**. A script rather than
# CTest's own WILL_FAIL property, because there are three outcomes and WILL_FAIL
# only distinguishes two -- and the third is the one that matters most:
#
#   * the probe aborts                 -> pass. The assertion fired, which is
#     the claim: the run-time half of the guard is live and does its job.
#   * the probe returns normally       -> FAIL, printing what came back. The
#     count wrapped and nothing stopped it.
#   * the probe says SKIPPED           -> SKIPPED. Under NDEBUG the assertion
#     expands to nothing, so the run-time half of the guard genuinely does not
#     exist and there is nothing here to check. CTest reports it as a skip
#     rather than as a pass, because a test that passes without checking
#     anything looks exactly like one that checked (ADR 0005).

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "pass -DPROBE=<path to the count_wraparound_probe executable>")
endif()

execute_process(
        COMMAND "${PROBE}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
)

if(stdout_text MATCHES "SKIPPED:")
    message("SKIPPED: the probe reports that assertions are not live in this tree")
    return()
endif()

if(exit_code EQUAL 0)
    message(FATAL_ERROR
            "the count wraparound probe returned normally, so nothing refused the "
            "wraparound at run time. Its output was:\n${stdout_text}${stderr_text}")
endif()

# `exit_code` is a signal name on POSIX and a number on Windows; either is worth
# printing, because it says how the process died.
message(STATUS "the guard fired: the probe did not return (${exit_code})")
