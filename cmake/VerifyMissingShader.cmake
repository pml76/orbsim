# Run the application against a shader directory that does not exist, and
# require that it refuses to start and says which file it could not open.
#
#     cmake -DORBSIM=<path to orbsim> -DMISSING=<a directory that does not exist>
#           -P cmake/VerifyMissingShader.cmake
#
# Run by CTest as `shader_missing_is_reported` (M1-13, register decision 148).
# A build tree can be incomplete -- a shader that failed to compile, a copy
# made without its shaders/ directory -- and the claim is that such a tree
# says so, **by path**, rather than asserting or starting with no pipelines.
#
# A script rather than CTest's WILL_FAIL or PASS_REGULAR_EXPRESSION, because
# the claim has two halves and each property checks one: WILL_FAIL passes on
# any failure at all, including a missing graphics card, and a regular
# expression alone ignores the exit code. Both are checked here.

if(NOT DEFINED ORBSIM OR NOT DEFINED MISSING)
    message(FATAL_ERROR "pass -DORBSIM=<path to orbsim> -DMISSING=<a directory that does not exist>")
endif()

# The instrument, checked before it is trusted: if the directory exists, the
# run below proves nothing.
if(EXISTS "${MISSING}")
    message(FATAL_ERROR "'${MISSING}' exists, so it cannot stand for a missing shader directory")
endif()

execute_process(
        COMMAND "${ORBSIM}" --validate --seconds 1 --shader-dir "${MISSING}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
)

# 1 is kExitFailure in src/app/main.cpp. Not 0, which would mean it ran; not 2,
# which would mean the argument was not understood; not 3, which would mean it
# ran and the validation layers complained.
if(NOT exit_code EQUAL 1)
    message(FATAL_ERROR
            "expected exit code 1 for a missing shader directory, got '${exit_code}'. "
            "Output:\n${stdout_text}${stderr_text}")
endif()

# The first file the renderer reads is line.vert.spv, and the message must
# name it with the directory it was looked for in. Matched as two literals
# rather than one path, because on Windows the directory arrives with forward
# slashes and the file is joined to it with a backslash.
get_filename_component(missing_name "${MISSING}" NAME)
string(FIND "${stderr_text}" "${missing_name}" directory_at)
string(FIND "${stderr_text}" "line.vert.spv" file_at)
if(directory_at EQUAL -1 OR file_at EQUAL -1)
    message(FATAL_ERROR
            "the failure did not name the missing file. Expected '${missing_name}' and "
            "'line.vert.spv' on stderr, which was:\n${stderr_text}")
endif()
