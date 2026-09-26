# Run one probe under the validation layers and check what it wrote (M1-16;
# ADR 0008; register decisions 192-194).
#
#     cmake -DORBSIM=<path to orbsim> -DPROBE=<name> -DOUT=<directory>
#           [-DCOMPARE_WITH=<an earlier run's directory>]
#           -P cmake/RunProbe.cmake
#
# Run by CTest as `probe_clear` and, with COMPARE_WITH, as
# `probe_clear_determinism`.
#
# **The files are checked whatever the exit code, and before it** (decision
# 193): a probe writes its five files on every run, pass or fail, so a run
# that failed is reported with what it left behind rather than instead of it.
# Then the exit code must be 0. With COMPARE_WITH, the HDR dump must be byte
# for byte the one the earlier run wrote -- determinism as a tested property
# (VERIFICATION.md rule 16): a frame that depends on the clock, on memory
# nobody initialised, or on a race is caught here.
#
# A script rather than CTest's own properties because the claim has three
# halves -- the files, the exit code, the comparison -- and each property
# checks at most one.

if(NOT DEFINED ORBSIM OR NOT DEFINED PROBE OR NOT DEFINED OUT)
    message(FATAL_ERROR "pass -DORBSIM=<path to orbsim> -DPROBE=<name> -DOUT=<directory>")
endif()

# Every file this run is to write is removed first, so that a file left by an
# earlier run cannot pass for one this run wrote.
set(files
        "${PROBE}.hdr.f32"
        "${PROBE}.png"
        "${PROBE}.16.png"
        "${PROBE}.exr"
        "${PROBE}.txt")
foreach(file IN LISTS files)
    file(REMOVE "${OUT}/${file}")
endforeach()

execute_process(
        COMMAND "${ORBSIM}" --validate --probe "${PROBE}" --probe-out "${OUT}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
)

set(missing "")
foreach(file IN LISTS files)
    set(path "${OUT}/${file}")
    if(NOT EXISTS "${path}")
        list(APPEND missing "${file} (absent)")
    else()
        file(SIZE "${path}" size)
        if(size EQUAL 0)
            list(APPEND missing "${file} (empty)")
        endif()
    endif()
endforeach()

if(missing)
    message(FATAL_ERROR
            "probe ${PROBE} exited with '${exit_code}' and did not write: ${missing}. "
            "Output:\n${stdout_text}${stderr_text}")
endif()
message(STATUS "probe ${PROBE}: all five files written to ${OUT}, exit code ${exit_code}")

# 0 is a clean run. 1 is kExitFailure, 2 kExitUsage and 3
# kExitValidationErrors, in src/app/ExitCodes.hpp.
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR
            "probe ${PROBE} wrote its files but exited with '${exit_code}'. "
            "Output:\n${stdout_text}${stderr_text}")
endif()

if(DEFINED COMPARE_WITH)
    set(this_dump "${OUT}/${PROBE}.hdr.f32")
    set(earlier_dump "${COMPARE_WITH}/${PROBE}.hdr.f32")
    # The instrument, checked before it is trusted: comparing a file with
    # itself would pass whatever it held.
    get_filename_component(this_real "${this_dump}" REALPATH)
    get_filename_component(earlier_real "${earlier_dump}" REALPATH)
    if(this_real STREQUAL earlier_real)
        message(FATAL_ERROR "OUT and COMPARE_WITH name the same directory; the comparison would prove nothing")
    endif()
    if(NOT EXISTS "${earlier_dump}")
        message(FATAL_ERROR "no earlier dump at ${earlier_dump} to compare with")
    endif()
    execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files "${this_dump}" "${earlier_dump}"
            RESULT_VARIABLE different
    )
    if(NOT different EQUAL 0)
        message(FATAL_ERROR
                "probe ${PROBE} rendered differently twice: ${this_dump} and ${earlier_dump} "
                "are not byte-identical. A probe that renders differently twice is a bug in the "
                "probe (ADR 0008).")
    endif()
    message(STATUS "probe ${PROBE}: the HDR dump is byte-identical to ${earlier_dump}")
endif()
