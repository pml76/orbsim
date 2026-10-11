# Run a probe that must abort on an assertion, and decide which way round its
# exit code means.
#
#     cmake -DPROBE=<path to the probe> "-DCLAIM=<what the assertion guards>"
#           -P cmake/VerifyAssertionProbe.cmake
#
# Written for the count-wraparound probes (M1-12, register decision 136) as
# `VerifyCountWraparound.cmake`, and generalised by M1-118 (register decision
# 476) so that every probe of this kind -- `angleBetween`'s zero vector was the
# second -- is judged by the one script, with its subtle half in one place.
# CLAIM is only ever printed: it names what the guard protects, so a failure
# says which one did not fire.
#
# A probe provokes something an `ORBSIM_EXPECTS` asserts against, so **the probe
# failing is the pass**. A script rather than CTest's own WILL_FAIL property,
# because there are three outcomes and WILL_FAIL only distinguishes two -- and
# the third is the one that matters most:
#
#   * the probe aborts                 -> pass. The assertion fired, which is
#     the claim: the guard is live and does its job.
#   * the probe returns normally       -> FAIL, printing what came back. The
#     guard did not fire and nothing stopped the bad value.
#   * the probe says SKIPPED           -> SKIPPED. Under NDEBUG the assertion
#     expands to nothing, so the guard genuinely does not exist and there is
#     nothing here to check. CTest reports it as a skip rather than as a pass,
#     because a test that passes without checking anything looks exactly like
#     one that checked (ADR 0005).
#
# Every probe follows the same conventions: it prints "SKIPPED:" when
# assertions are not live, "the guard did not fire" when it comes back, and
# returns 1 rather than 0 in that case.

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "pass -DPROBE=<path to the probe executable>")
endif()
if(NOT DEFINED CLAIM)
    message(FATAL_ERROR "pass -DCLAIM=<what the probe's assertion guards>")
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

# **The probe's own verdict is read first, and this is the half that was
# missing.** Written on 2026-09-23 with only the exit-code test below, which
# was wrong in the one case that matters: the probe says "the guard did not
# fire" and returns **1**, and a check for `EQUAL 0` calls that a pass. So the
# test that exists to close M1-12's declared survivor reported green against a
# tree carrying that very survivor -- measured 2026-09-24, by applying the
# mutant by hand and running this entry, which is the only reason it was found.
#
# A check that silently stops checking looks exactly like one that passes
# (VERIFICATION.md rule 23), and this one looked like it for a day.
if(stdout_text MATCHES "the guard did not fire")
    message(FATAL_ERROR
            "${CLAIM}: the guard did not fire, and nothing stopped the value. "
            "The probe's own report:\n${stdout_text}${stderr_text}")
endif()

# **An abort only counts if it is the assertion's.** ORBSIM_EXPECTS writes its
# condition with "precondition violated" beside it, and both the MSVC and the
# glibc runtime print that on stderr as they abort. Anything else that ends the
# process -- a failure to print, which inside a noexcept function would be
# std::terminate, and so an abort too -- would otherwise read as the guard
# firing. Added 2026-09-24, when the probe moved from printf to std::println,
# which can throw (register decision 156).
if(NOT exit_code EQUAL 0 AND NOT stderr_text MATCHES "precondition violated")
    message(FATAL_ERROR
            "${CLAIM}: the probe ended abnormally without the assertion's message, so "
            "its exit says nothing about the guard. Its output was:
${stdout_text}${stderr_text}")
endif()

if(exit_code EQUAL 0)
    message(FATAL_ERROR
            "${CLAIM}: the probe returned normally and said nothing about why, which "
            "it should never do. Its output was:\n${stdout_text}${stderr_text}")
endif()

# `exit_code` is a signal name on POSIX and a number on Windows; either is worth
# printing, because it says how the process died.
message(STATUS "${CLAIM}: the guard fired; the probe did not return (${exit_code})")
