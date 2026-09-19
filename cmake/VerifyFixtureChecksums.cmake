# Verify generated reference data against its committed SHA-256 sums.
#
#     cmake -DDIRECTORY=data/horizons -P cmake/VerifyFixtureChecksums.cmake
#
# Run by CTest as `horizons_fixture_checksums`, so `check` proves that the
# fixtures on this machine are the ones the error budgets were measured
# against (register decision 44, VERIFICATION.md rule 21). `sha256sum -c
# checksums.sha256` answers the same question by hand; this answers it without
# needing sha256sum, which a Windows machine may not have on its PATH.
#
# Three outcomes, and none of them silent:
#
#   * a fixture present and its hash wrong  -> FAIL, naming both hashes.
#     Either the query drifted or Horizons changed -- a new ephemeris, a new
#     software revision -- and either way a person should look before any
#     budget is trusted again;
#   * a fixture absent                      -> SKIPPED, naming it and the README.
#     Horizons output is not redistributed, so a fresh clone has none; CTest
#     reports the skip in its summary rather than counting it as a pass
#     (ADR 0005: a step that silently stops checking looks exactly like one
#     that passes);
#   * every fixture present and matching    -> pass.
#
# sha256sum writes "<hash>  <name>" in text mode and "<hash> *<name>" in
# binary mode, which is what Git Bash's writes on Windows; both are accepted.

if(NOT DEFINED DIRECTORY)
    message(FATAL_ERROR "pass -DDIRECTORY=<the directory holding checksums.sha256>")
endif()
set(sums "${DIRECTORY}/checksums.sha256")
if(NOT EXISTS "${sums}")
    message(FATAL_ERROR "${sums} does not exist")
endif()

file(STRINGS "${sums}" entries)
set(missing "")
set(failed "")
set(verified 0)
foreach(entry IN LISTS entries)
    string(REGEX REPLACE "\r$" "" entry "${entry}")
    if(entry STREQUAL "")
        continue()
    endif()
    if(NOT entry MATCHES "^([0-9a-f]+) [ *](.+)$")
        message(FATAL_ERROR "${sums}: not a checksum line: '${entry}'")
    endif()
    set(expected "${CMAKE_MATCH_1}")
    set(name "${CMAKE_MATCH_2}")
    string(LENGTH "${expected}" digits)
    if(NOT digits EQUAL 64)
        message(FATAL_ERROR "${sums}: '${expected}' is not a SHA-256 sum")
    endif()
    if(NOT EXISTS "${DIRECTORY}/${name}")
        list(APPEND missing "${name}")
        continue()
    endif()
    file(SHA256 "${DIRECTORY}/${name}" actual)
    if(NOT actual STREQUAL expected)
        list(APPEND failed "${name}: committed ${expected}, generated ${actual}")
    else()
        math(EXPR verified "${verified} + 1")
    endif()
endforeach()

if(failed)
    list(JOIN failed "\n  " detail)
    message(FATAL_ERROR
            "A generated fixture is not the one its budgets were measured against:\n"
            "  ${detail}\n"
            "Regenerate it as data/horizons/README.md says, or find out what changed.")
endif()
if(missing)
    list(JOIN missing ", " names)
    message(STATUS
            "SKIPPED: not generated on this machine: ${names}. "
            "See data/horizons/README.md. ${verified} other fixture(s) verified.")
    return()
endif()
message(STATUS "All ${verified} fixture(s) match their committed SHA-256 sums.")
