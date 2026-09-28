# Refuse a build tree set up from text that is no longer on disk (M1-98,
# register decision 225).
#
# CMake reads CMakeLists.txt when a configure starts and writes the build plan,
# build.ninja, when it ends. An edit that lands in between -- CLion reloading on
# its own while the file is being edited, or a mutation pass writing it -- leaves
# a plan built from the old text but dated after the new one. Ninja decides
# whether to configure again by comparing those dates, so it never does, and
# `check` builds and tests the old text without a word. Reproduced on
# 2026-09-28 in a scratch project: the build printed the text from before the
# edit while the file on disk said otherwise.
#
# Two refusals close it. `orbsim_configure_input` takes the file's SHA-256 --
# a fingerprint of its exact bytes -- as soon as it has been read, and
# `orbsim_configure_inputs_unchanged` compares every fingerprint again at the
# end of the configure: an edit during the configure fails it, and a failed
# configure leaves the old build.ninja, older than the edit, so the next build
# configures again by itself. The fingerprints are then written to the tree,
# where the `configure-current` step of `check`
# (scripts/check-configure-current.py) compares them with the files on disk
# once more. That catches an edit during CMake's last step, writing the build
# plan, which comes after the end of the configure.
#
# What neither can catch: an edit in the milliseconds between CMake reading a
# file and the fingerprint line near its top running. CMake offers no earlier
# hook. The window is that short because the fingerprint is taken before
# project(), which is what takes seconds.
#
# Tested by `configure_guard` (cmake/TestConfigureGuard.cmake), which configures
# a scratch project that edits its own CMakeLists.txt mid-configure.

function(orbsim_configure_input path)
    file(SHA256 "${path}" hash)
    set_property(GLOBAL APPEND PROPERTY ORBSIM_CONFIGURE_INPUTS "${hash} ${path}")
endfunction()

# Last in the configure, so that everything it read has been read.
function(orbsim_configure_inputs_unchanged record_file)
    get_property(inputs GLOBAL PROPERTY ORBSIM_CONFIGURE_INPUTS)
    set(record "")
    foreach(entry IN LISTS inputs)
        string(SUBSTRING "${entry}" 0 64 was)
        string(SUBSTRING "${entry}" 65 -1 path)
        file(SHA256 "${path}" now)
        if(NOT now STREQUAL was)
            message(FATAL_ERROR
                    "${path} changed while this tree was being configured, so the build plan "
                    "would come from text that is no longer on disk. Configure again:\n"
                    "  cmake -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}")
        endif()
        # sha256sum's format: the fingerprint, two spaces, the path.
        string(APPEND record "${was}  ${path}\n")
    endforeach()
    file(WRITE "${record_file}" "${record}")
endfunction()
