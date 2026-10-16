# Hold one real read record to what it must say (M1-129, register decisions
# 498, 527 and 528).
#
#     cmake -DPROGRAM=<test_read_records> -DRECORDS=<build tree>/test-reads
#           -DDATA=<source tree>/data -P cmake/VerifyReadRecord.cmake
#
# Run by CTest as `read_record_written`. It runs the hidden case
# "record check: a/b <c>" in tests/test_read_records.cpp alone -- the case
# names data/skyfield/README.md twice and a file that is not there once, and
# the suite's own listener names data/horizons/README.md as the run starts,
# before any test case (register decision 604) -- and requires the record it
# leaves to be exactly:
#
#     <SHA-256 of horizons/README.md> <DATA>/horizons/README.md
#     <SHA-256 of skyfield/README.md> <DATA>/skyfield/README.md
#     missing <DATA>/skyfield/no-such-file.txt
#     end
#
# The first line is the rule that a read outside any test case belongs to
# every record written after it: whatever a program read is an input of each
# case it runs.
#
# under the file name `record check%3A a%2Fb %3Cc%3E.txt`. Each part is
# checked against a second implementation: the name spelled out here by hand
# from decision 528's rule, the hash computed by CMake's own file(SHA256)
# rather than tests/Sha256.hpp. A record that left out a file, hashed it
# wrongly, wrote it twice, forgot `end`, or went under a differently escaped
# name fails here -- where in `check` it would only make a test that should
# run look unchanged.

foreach(variable IN ITEMS PROGRAM RECORDS DATA)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "pass -DPROGRAM=<test_read_records> -DRECORDS=<tree>/test-reads -DDATA=<data>")
    endif()
endforeach()

set(record "${RECORDS}/record check%3A a%2Fb %3Cc%3E.txt")
file(REMOVE "${record}")
execute_process(COMMAND "${PROGRAM}" "record check: a/b <c>"
        RESULT_VARIABLE exit_code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "the case failed (exit ${exit_code}):\n${out}${err}")
endif()
if(NOT EXISTS "${record}")
    message(FATAL_ERROR "the case left no record at\n  ${record}\nso its name was escaped otherwise "
            "than decision 528 says, or the listener wrote nothing")
endif()

file(READ "${record}" written)
file(SHA256 "${DATA}/horizons/README.md" read_at_start)
file(SHA256 "${DATA}/skyfield/README.md" readme)
string(CONCAT expected "${read_at_start} ${DATA}/horizons/README.md\n"
        "${readme} ${DATA}/skyfield/README.md\n" "missing ${DATA}/skyfield/no-such-file.txt\n" "end\n")
if(NOT written STREQUAL expected)
    message(FATAL_ERROR "the record is not what the case read.\nwritten:\n${written}\nexpected:\n${expected}")
endif()
message(STATUS "the record names what the case read, hashed as file(SHA256) hashes it")
