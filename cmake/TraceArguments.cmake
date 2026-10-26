# The arguments of CMake's trace for a `cmake -P` test (M1-138, register
# decisions 768, 784, 798, 808, 809 and 810).
#
# The trace is the read record of such a test. CMake writes each command that
# the script runs, with its arguments expanded, and scripts/run-tests.py reads
# from it the files that the test read. Thus a change in scripts/ runs the test
# again only when the test read the changed file. The trace goes to
# <tree>/test-reads/<test name>.trace.json, with the read records of the other
# tests. The three arguments come before -D and -P on the command line,
# because CMake reads its options before -P (decision 809 (4)).
#
# A test name that the escape of decision 528 changes stops the configure
# (decision 798): the driver finds the trace by the escaped name, and the name
# of the file here is the test name itself. The escape changes each character
# below 0x20, each of < > : " / \ | ? * %, and a last . or space.
#
# When the folder of the trace is not there, CMake stops `cmake -P` before the
# script runs (measured with CMake 4.3.1). Thus this file makes test-reads/ at
# the configure (decision 810). Else, in a new tree, only the first record of a
# C++ test makes it, or the driver before its run of CTest, and a plain `ctest`
# can start a `cmake -P` test before the two. The end-to-end test of the driver
# includes this file too (decision 808).

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-reads")

function(orbsim_trace_arguments out_var test_name)
    set(changed "")
    string(LENGTH "${test_name}" length)
    if(length GREATER 0)
        math(EXPR last "${length} - 1")
        string(SUBSTRING "${test_name}" ${last} 1 end)
        if(end STREQUAL "." OR end STREQUAL " ")
            set(changed "it ends with \"${end}\"")
        endif()
    endif()
    foreach(character "<" ">" ":" "\"" "/" "\\" "|" "?" "*" "%")
        string(FIND "${test_name}" "${character}" at)
        if(NOT at EQUAL -1)
            set(changed "it holds \"${character}\"")
        endif()
    endforeach()
    foreach(code RANGE 1 31)
        string(ASCII ${code} character)
        string(FIND "${test_name}" "${character}" at)
        if(NOT at EQUAL -1)
            set(changed "it holds the control character ${code}")
        endif()
    endforeach()
    if(changed)
        message(FATAL_ERROR "orbsim_trace_arguments: the escape of register decision 528 changes the test name "
                            "\"${test_name}\", because ${changed}. Thus scripts/run-tests.py cannot find its trace.")
    endif()
    set(${out_var} --trace-expand --trace-format=json-v1
            "--trace-redirect=${CMAKE_BINARY_DIR}/test-reads/${test_name}.trace.json" PARENT_SCOPE)
endfunction()
