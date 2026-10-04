# Run a short benchmark under the validation layers and check what it wrote
# (M1-22, register decisions 372 and 373).
#
#     cmake -DORBSIM=<path to orbsim> -DOUT=<directory> -DFRAMES=<n>
#           -P cmake/RunBench.cmake
#
# Run by CTest as `orbsim_bench_smoke`. **It asserts no frame time** -- that is
# register decision 5, and the task's own "nothing in check asserts a frame
# time". What it asserts is that the instrument runs: the timestamp queries,
# the swapchain that does not wait for the display and the report all go
# through the validation layers without a complaint, the run ends with exit
# code 0, and it writes one summary and one table, the table with a header,
# FRAMES measured rows, at least one warm-up row, and six fields in every row
# -- none of them a NaN or an infinity, which is what a frame whose GPU time
# was never read would print, and no GPU time longer than the whole run.
#
# A script rather than CTest's own properties for the reason
# cmake/VerifyMissingShader.cmake gives: the claim has several halves.

if(NOT DEFINED ORBSIM OR NOT DEFINED OUT OR NOT DEFINED FRAMES)
    message(FATAL_ERROR "pass -DORBSIM=<path to orbsim> -DOUT=<directory> -DFRAMES=<n>")
endif()

# An earlier run's files are removed first, so that one of them cannot pass
# for one this run wrote.
file(REMOVE_RECURSE "${OUT}")

execute_process(
        COMMAND "${ORBSIM}" --bench grid-orbit --frames ${FRAMES} --validate --bench-out "${OUT}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "expected exit code 0, got '${exit_code}'. "
                        "Output:\n${stdout_text}${stderr_text}")
endif()

file(GLOB summaries "${OUT}/grid-orbit-*.txt")
file(GLOB tables "${OUT}/grid-orbit-*.csv")
list(LENGTH summaries summary_count)
list(LENGTH tables table_count)
if(NOT summary_count EQUAL 1 OR NOT table_count EQUAL 1)
    message(FATAL_ERROR "expected one .txt and one .csv in ${OUT}, found "
                        "${summary_count} and ${table_count}")
endif()

# The summary says what it was measured under (decision 372).
file(READ "${summaries}" summary)
foreach(expected "Validation:   on"
                 "NOTE: the validation layers were on"
                 "VK_PRESENT_MODE_"
                 "frame interval"
                 "CPU working"
                 "GPU ")
    string(FIND "${summary}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "the summary does not say '${expected}':\n${summary}")
    endif()
endforeach()

file(STRINGS "${tables}" rows)
list(POP_FRONT rows header)
if(NOT header STREQUAL "frame,path_time_s,phase,interval_ms,cpu_working_ms,gpu_ms")
    message(FATAL_ERROR "the table's header is '${header}'")
endif()
set(measured 0)
set(warm_up 0)
foreach(row IN LISTS rows)
    string(REPLACE "," ";" fields "${row}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 6)
        message(FATAL_ERROR "a row of the table has ${field_count} fields: '${row}'")
    endif()
    string(TOLOWER "${row}" lower)
    if(lower MATCHES "nan|inf")
        message(FATAL_ERROR "a row of the table is not all numbers: '${row}'")
    endif()
    list(GET fields 2 phase)
    if(phase STREQUAL "measured")
        math(EXPR measured "${measured} + 1")
    elseif(phase STREQUAL "warm-up")
        math(EXPR warm_up "${warm_up} + 1")
    else()
        message(FATAL_ERROR "a row of the table has the phase '${phase}'")
    endif()
endforeach()
# No frame's GPU time may exceed the whole run's length (register decision
# 380). Not a performance threshold, which decision 5 rules out: a figure no
# real frame can have, and the one timestamps read the wrong way round
# produce, about 1.8e11 ms. CMake's arithmetic is in whole numbers, so the
# run's length is bounded from above by each interval's whole milliseconds
# plus one, and a GPU time is compared by its whole milliseconds; a figure
# written with a positive exponent is too large by itself.
set(run_ms 0)
set(gpu_values "")
foreach(row IN LISTS rows)
    string(REPLACE "," ";" fields "${row}")
    list(GET fields 3 interval)
    list(GET fields 5 gpu)
    if(interval MATCHES "e\\+" OR gpu MATCHES "e\\+")
        message(FATAL_ERROR "a row of the table has a figure past any frame's: '${row}'")
    endif()
    string(REGEX MATCH "^[0-9]+" interval_whole "${interval}")
    math(EXPR run_ms "${run_ms} + ${interval_whole} + 1")
    list(APPEND gpu_values "${gpu}")
endforeach()
foreach(gpu IN LISTS gpu_values)
    string(REGEX MATCH "^[0-9]+" gpu_whole "${gpu}")
    if(gpu_whole GREATER run_ms)
        message(FATAL_ERROR "a frame's GPU time, ${gpu} ms, is longer than the whole run, "
                            "at most ${run_ms} ms")
    endif()
endforeach()

if(NOT measured EQUAL FRAMES)
    message(FATAL_ERROR "expected ${FRAMES} measured rows, found ${measured}")
endif()
if(warm_up LESS 1)
    message(FATAL_ERROR "expected at least one warm-up row, found none")
endif()
