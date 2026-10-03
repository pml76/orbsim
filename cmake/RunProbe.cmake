# Run one probe under the validation layers and check what it wrote (M1-16;
# ADR 0008; register decisions 192-194), and since M1-17 what it made of its
# golden image (register decisions 230, 231 and 236), and since M1-110 the
# golden of the graphics card it ran on (decisions 287-299).
#
#     cmake -DORBSIM=<path to orbsim> -DPROBE=<name> -DOUT=<directory>
#           [-DCOMPARE_WITH=<an earlier run's directory>]
#           [-DGOLDEN=<golden PNG> | -DGOLDEN_DIR=<directory of card folders>]
#           [-DEXPECT_EXIT=<code>] [-DEXPECT_DIFF=ON] [-DPLANT_DIFF=ON] [-DEXPECT_CARD=ON]
#           [-DEXPECT_STDOUT=<text>|<text>...] [-DEXPECT_STDERR=<text>|<text>...]
#           -P cmake/RunProbe.cmake
#
# Run by CTest as `probe_clear`, with COMPARE_WITH as `probe_clear_determinism`,
# with GOLDEN_DIR as `probe_clear_golden`, and with a broken or missing golden
# as the `probe_golden_*` tests.
#
# **The files are checked whatever the exit code, and before it** (decision
# 193): a probe writes its five files on every run, pass or fail, so a run
# that failed is reported with what it left behind rather than instead of it.
# Then the exit code must be EXPECT_EXIT, 0 unless said otherwise. The diff
# image must exist exactly when EXPECT_DIFF is ON: a mismatch has to be
# diagnosable from the files, and a run that matched must not leave one. With
# COMPARE_WITH, the HDR dump must be byte for byte the one the earlier run
# wrote -- determinism as a tested property (VERIFICATION.md rule 16): a
# frame that depends on the clock, on memory nobody initialised, or on a race
# is caught here. With PLANT_DIFF, a diff image is left where this run's
# would go before it starts, standing for one from an earlier failed run: the
# application must remove it (decision 231), and a run that matches must end
# without one. EXPECT_STDOUT and EXPECT_STDERR are texts, separated by `|`,
# that the run must have printed -- on stderr for a mismatch, where decision
# 230 puts the two measured numbers. With EXPECT_CARD, stderr must name the
# card's folder, `<vendor>-<device>`, built here from the sidecar's own
# gpu.vendor and gpu.device lines rather than taken from the application's
# message -- so a card named by the wrong numbers is caught, on any card.
#
# A script rather than CTest's own properties because the claim has several
# halves -- the files, the exit code, the output, the comparison -- and each
# property checks at most one.

if(NOT DEFINED ORBSIM OR NOT DEFINED PROBE OR NOT DEFINED OUT)
    message(FATAL_ERROR "pass -DORBSIM=<path to orbsim> -DPROBE=<name> -DOUT=<directory>")
endif()
if(NOT DEFINED EXPECT_EXIT)
    set(EXPECT_EXIT 0)
endif()

# Every file this run is to write is removed first, so that a file left by an
# earlier run cannot pass for one this run wrote.
set(files
        "${PROBE}.hdr.f32"
        "${PROBE}.png"
        "${PROBE}.16.png"
        "${PROBE}.exr"
        "${PROBE}.txt")
set(diff "${PROBE}.diff.png")
foreach(file IN LISTS files diff)
    file(REMOVE "${OUT}/${file}")
endforeach()
if(PLANT_DIFF)
    file(WRITE "${OUT}/${diff}" "a diff image left by an earlier run")
endif()

set(golden_arguments "")
if(DEFINED GOLDEN AND DEFINED GOLDEN_DIR)
    message(FATAL_ERROR "pass GOLDEN or GOLDEN_DIR, not both")
elseif(DEFINED GOLDEN)
    set(golden_arguments --golden "${GOLDEN}")
elseif(DEFINED GOLDEN_DIR)
    set(golden_arguments --golden-dir "${GOLDEN_DIR}")
endif()
execute_process(
        COMMAND "${ORBSIM}" --validate --probe "${PROBE}" --probe-out "${OUT}" ${golden_arguments}
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

# 0 is a clean run. 1 is kExitFailure, 2 kExitUsage, 3 kExitValidationErrors
# and 4 kExitGoldenMismatch, in src/app/ExitCodes.hpp.
if(NOT exit_code EQUAL EXPECT_EXIT)
    message(FATAL_ERROR
            "probe ${PROBE} wrote its files and exited with '${exit_code}', not ${EXPECT_EXIT}. "
            "Output:\n${stdout_text}${stderr_text}")
endif()

set(diff_path "${OUT}/${diff}")
if(EXPECT_DIFF)
    if(NOT EXISTS "${diff_path}")
        message(FATAL_ERROR "probe ${PROBE} reported a mismatch and wrote no ${diff}. "
                            "Output:\n${stdout_text}${stderr_text}")
    endif()
    file(SIZE "${diff_path}" size)
    if(size EQUAL 0)
        message(FATAL_ERROR "probe ${PROBE} wrote an empty ${diff}")
    endif()
    message(STATUS "probe ${PROBE}: ${diff} written")
elseif(EXISTS "${diff_path}")
    message(FATAL_ERROR "probe ${PROBE} left a ${diff} although no mismatch was expected. "
                        "Output:\n${stdout_text}${stderr_text}")
endif()

# Each expected text, found in the stream it belongs to. `|` separates them,
# because a `;` does not survive the trip through add_test.
foreach(stream IN ITEMS STDOUT STDERR)
    if(NOT DEFINED EXPECT_${stream})
        continue()
    endif()
    string(TOLOWER "${stream}" name)
    string(REPLACE "|" ";" expected_texts "${EXPECT_${stream}}")
    foreach(text IN LISTS expected_texts)
        string(FIND "${${name}_text}" "${text}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "probe ${PROBE} did not print '${text}' on ${name}, which was:\n"
                                "${${name}_text}")
        endif()
    endforeach()
endforeach()

if(EXPECT_CARD)
    file(STRINGS "${OUT}/${PROBE}.txt" vendor_line REGEX "^gpu\\.vendor +=")
    file(STRINGS "${OUT}/${PROBE}.txt" device_line REGEX "^gpu\\.device +=")
    string(REGEX MATCH "0x([0-9a-f]+)$" vendor_hex "${vendor_line}")
    set(vendor_hex "${CMAKE_MATCH_1}")
    string(REGEX MATCH "0x([0-9a-f]+)$" device_hex "${device_line}")
    set(device_hex "${CMAKE_MATCH_1}")
    if(vendor_hex STREQUAL "" OR device_hex STREQUAL "")
        message(FATAL_ERROR "probe ${PROBE}'s sidecar has no gpu.vendor and gpu.device to "
                            "check the card's name against: '${vendor_line}' '${device_line}'")
    endif()
    set(card "${vendor_hex}-${device_hex}")
    string(FIND "${stderr_text}" "(Vulkan ${card})" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "probe ${PROBE} did not name the card as ${card}, from its "
                            "sidecar, on stderr, which was:\n${stderr_text}")
    endif()
    message(STATUS "probe ${PROBE}: named the card ${card}")
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
