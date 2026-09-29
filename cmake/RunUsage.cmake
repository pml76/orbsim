# Run the application with arguments it must refuse, and require that it
# refuses them as a usage error and says why (M1-17, register decisions 232
# and 233).
#
#     cmake -DORBSIM=<path to orbsim> -DARGS=<argument>|<argument>...
#           -DEXPECT=<text stderr must hold> -P cmake/RunUsage.cmake
#
# Run by CTest as the `usage_*` tests. The arguments are separated by `|`,
# because a `;` does not survive the trip through add_test. Nothing here
# needs a device: the arguments are refused before a window exists.
#
# A script rather than CTest's WILL_FAIL or PASS_REGULAR_EXPRESSION for the
# reason cmake/VerifyMissingShader.cmake gives: the claim has two halves, the
# exit code and the message, and each property checks one.

if(NOT DEFINED ORBSIM OR NOT DEFINED ARGS OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "pass -DORBSIM=<path to orbsim> -DARGS=<arguments> -DEXPECT=<text>")
endif()
string(REPLACE "|" ";" arguments "${ARGS}")

execute_process(
        COMMAND "${ORBSIM}" ${arguments}
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
)

# 2 is kExitUsage in src/app/ExitCodes.hpp.
if(NOT exit_code EQUAL 2)
    message(FATAL_ERROR "expected exit code 2 (usage) for '${ARGS}', got '${exit_code}'. "
                        "Output:\n${stdout_text}${stderr_text}")
endif()
string(FIND "${stderr_text}" "${EXPECT}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the refusal of '${ARGS}' did not say '${EXPECT}'; stderr was:\n"
                        "${stderr_text}")
endif()
