# What a failed precondition or postcondition says (M1-129, register decisions
# 549, 556 and 557).
#
#     cmake -DPROBE=<tests/assertion_message_probe> -DSOURCE=<its .cpp>
#           -DKIND=precondition|postcondition -DFUNCTION=<the function it trips in>
#           -DLINES=ON|OFF -P cmake/VerifyAssertionMessage.cmake
#
# The message must name what does not move when a line is added above the
# assertion -- "<kind> violated", the condition's text, the function and the
# file -- and, where the tree has debug information to read it from (LINES=ON,
# every tree but MSVC's, which links without it, register decision 511), the
# line the assertion is on, through the chain of calls std::stacktrace prints.
#
# **Each part is looked for where it belongs** (register decision 622): the
# condition, the function and the file in the message above the chain's
# heading, and the line in the chain's first frame, which the heading -- "the
# failing line first" -- promises. The chain's first frame names the function
# and the file as well, and a chain taken inside the handler rather than
# inside the macro begins with the handler's own frame and names the failing
# line one frame down. So a search of the whole output passed a message that
# had lost its function or its file, and a chain taken in the handler: three
# of m1-129-debug.json's mutants, measured 2026-10-17 in the Windows clang
# Debug build and with Linux gcc-14 and clang.
#
# **The line is found in the probe's source, not in its output.** The mark
# "assertion-message-probe: the <kind>'s line" sits on the assertion's own
# line, and this script counts the lines before it: a second route to the
# number, so a message that names the wrong line fails.
#
# The line appears as `file(23)` from Microsoft's standard library and as
# `file:23` from libstdc++'s, and either is accepted.

foreach(required PROBE SOURCE KIND FUNCTION LINES)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "pass -D${required}=...")
    endif()
endforeach()

execute_process(
        COMMAND "${PROBE}" "${KIND}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
)

if(stdout_text MATCHES "SKIPPED:")
    message("SKIPPED: the probe reports that assertions are not live in this tree")
    return()
endif()
if(stdout_text MATCHES "the guard did not fire" OR exit_code EQUAL 0)
    message(FATAL_ERROR "the ${KIND} did not fire; the probe's report:\n${stdout_text}${stderr_text}")
endif()

# The line the mark is on, counted in the source.
file(READ "${SOURCE}" source_text)
string(FIND "${source_text}" "assertion-message-probe: the ${KIND}'s line" mark_at)
if(mark_at EQUAL -1)
    message(FATAL_ERROR "${SOURCE} carries no mark for the ${KIND}'s line")
endif()
string(SUBSTRING "${source_text}" 0 ${mark_at} before_mark)
string(REGEX MATCHALL "\n" newlines "${before_mark}")
list(LENGTH newlines line)
math(EXPR line "${line} + 1")

# The message is the text above the chain's heading; the chain's first frame
# is the first line after it.
set(heading "the chain of calls, the failing line first:")
string(FIND "${stderr_text}" "${heading}" heading_at)
if(heading_at EQUAL -1)
    message(FATAL_ERROR "the ${KIND}'s message has no \"${heading}\". It was:\n${stderr_text}")
endif()
string(SUBSTRING "${stderr_text}" 0 ${heading_at} message_text)
string(LENGTH "${heading}" heading_length)
math(EXPR chain_at "${heading_at} + ${heading_length}")
string(SUBSTRING "${stderr_text}" ${chain_at} -1 chain_text)
string(STRIP "${chain_text}" chain_text)
string(FIND "${chain_text}" "\n" first_end)
string(SUBSTRING "${chain_text}" 0 ${first_end} first_frame)

get_filename_component(file_name "${SOURCE}" NAME)
set(missing "")
foreach(wanted "${KIND} violated" "${FUNCTION}" "${file_name}")
    string(FIND "${message_text}" "${wanted}" at)
    if(at EQUAL -1)
        list(APPEND missing "\"${wanted}\"")
    endif()
endforeach()
# The condition's text, as each probe writes it.
if(KIND STREQUAL "precondition")
    set(condition "count == 0")
else()
    set(condition "doubled == 0")
endif()
string(FIND "${message_text}" "${condition}" at)
if(at EQUAL -1)
    list(APPEND missing "the condition \"${condition}\"")
endif()
if(LINES)
    string(FIND "${first_frame}" "${file_name}(${line})" msvc_form)
    string(FIND "${first_frame}" "${file_name}:${line}" gnu_form)
    if(msvc_form EQUAL -1 AND gnu_form EQUAL -1)
        list(APPEND missing "the line in the chain's first frame, ${file_name}(${line}) or ${file_name}:${line}")
    endif()
endif()

if(missing)
    list(JOIN missing ", " missing_text)
    message(FATAL_ERROR "the ${KIND}'s message lacks ${missing_text}. It was:\n${stderr_text}")
endif()
if(LINES)
    message(STATUS "the ${KIND}'s message names the condition, ${FUNCTION}, ${file_name} and line ${line}")
else()
    message(STATUS "the ${KIND}'s message names the condition, ${FUNCTION} and ${file_name}")
endif()
