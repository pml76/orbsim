# The configure-time half of M1-98's guard, proven on a scratch project
# (cmake/ConfigureInputs.cmake). Run as the CTest test `configure_guard`:
#
#   cmake -DMODULE=<ConfigureInputs.cmake> -DWORK=<scratch directory>
#         -DGENERATOR=<generator> -DMAKE_PROGRAM=<ninja> -P TestConfigureGuard.cmake
#
# Two configures of the same project. The steady one must succeed and record
# its CMakeLists.txt's real fingerprint; the edited one appends to its own
# CMakeLists.txt between the fingerprint and the end of the configure -- the
# CLion race, made deterministic -- and must fail, naming the change. Either
# half alone would pass a guard that always fails or never does.

foreach(var MODULE WORK GENERATOR MAKE_PROGRAM)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "configure_guard: ${var} is not set")
    endif()
endforeach()
# A Windows path's backslashes would be read as escapes once MODULE is written
# into the scratch CMakeLists.txt; CMake's own form uses forward slashes.
file(TO_CMAKE_PATH "${MODULE}" MODULE)
file(TO_CMAKE_PATH "${WORK}" WORK)

function(scratch_configure name edit out_result out_output)
    set(dir "${WORK}/${name}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}/src")
    file(WRITE "${dir}/src/CMakeLists.txt"
            "cmake_minimum_required(VERSION 3.28)\n"
            "include(\"${MODULE}\")\n"
            "orbsim_configure_input(\"\${CMAKE_CURRENT_LIST_FILE}\")\n"
            "project(guard NONE)\n"
            "${edit}\n"
            "orbsim_configure_inputs_unchanged(\"\${CMAKE_BINARY_DIR}/configured-from.sha256\")\n")
    execute_process(COMMAND ${CMAKE_COMMAND} -S "${dir}/src" -B "${dir}/build"
                            -G "${GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

scratch_configure(steady "" result output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "configure_guard: the steady configure failed:\n${output}")
endif()
file(SHA256 "${WORK}/steady/src/CMakeLists.txt" expected)
file(READ "${WORK}/steady/build/configured-from.sha256" record)
if(NOT record MATCHES "^${expected}  [^\n]*/steady/src/CMakeLists.txt\n$")
    message(FATAL_ERROR "configure_guard: the steady configure recorded\n${record}\n"
                        "not the fingerprint ${expected} of its CMakeLists.txt")
endif()

scratch_configure(edited
        "file(APPEND \"\${CMAKE_CURRENT_LIST_FILE}\" \"# edited while configuring\\n\")"
        result output)
if(result EQUAL 0)
    message(FATAL_ERROR "configure_guard: a CMakeLists.txt edited mid-configure was accepted")
endif()
if(NOT output MATCHES "changed while this tree was being configured")
    message(FATAL_ERROR "configure_guard: the edited configure failed, but not for the edit:\n${output}")
endif()
message(STATUS "configure_guard: the steady configure recorded its fingerprint, "
               "and the edited one was refused")
