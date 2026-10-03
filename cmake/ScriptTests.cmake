# The CTest tests that check this project's own scripts and build.
#
# Moved out of CMakeLists.txt unchanged by M1-105 (register decision 254), and
# included where it stood, in the same directory scope: every variable and
# target it uses or makes is the top-level file's, as before -- ORBSIM_TEST_JOBS
# among them, which the `check` target reads. It has a file of its own so that
# the mutants aimed at it -- scripts/mutants/m1-100.json and m1-102.json -- are
# due again when this file changes, and not on every edit to CMakeLists.txt.
# Registered with the configure guard (M1-98) before it is included.

# The harness's rebuild plan (M1-97, register decision 224): after restoring a
# mutant, rebuild what it built, less what the next mutant builds itself. The
# rule is one function, proven by its self-test; the loop that uses it is
# proven by the pass of scripts/mutants/m1-95.json.
if(ORBSIM_PYTHON)
    add_test(NAME mutate_self_test
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutate.py --self-test
    )
else()
    add_test(NAME mutate_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(mutate_self_test PROPERTIES LABELS fixtures)

# A tree set up from text no longer on disk is refused (M1-98, register
# decision 225). The configure fingerprints what it reads and fails if any of
# it changed before the end (cmake/ConfigureInputs.cmake); `configure-current`
# compares the recorded fingerprints with the files on disk once more, which
# catches an edit during CMake's last step, writing the build plan. `check`
# waits for it, and it has no inputs, so Ninja starts it at once -- though
# Ninja lets jobs it has already started finish, so a refusal can take as long
# as the longest of them: 27 s, measured on 2026-09-28. The same
# check as a CTest test, so a mutant can be judged by it; its self-test; and
# `configure_guard`, which configures a scratch project that edits its own
# CMakeLists.txt mid-configure. About 2 s together.
if(ORBSIM_PYTHON)
    add_custom_target(configure-current
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-configure-current.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "configure-current: every file the configure read is unchanged"
            VERBATIM
    )
    add_test(NAME configure_current
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-configure-current.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME configure_current_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-configure-current.py --self-test
    )
else()
    add_custom_target(configure-current
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "python was not found on PATH; configure-current cannot run"
            VERBATIM
    )
    add_test(NAME configure_current COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME configure_current_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
add_test(NAME configure_guard
        COMMAND ${CMAKE_COMMAND}
                -DMODULE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConfigureInputs.cmake
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/configure_guard
                -DGENERATOR=${CMAKE_GENERATOR}
                -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestConfigureGuard.cmake
)
set_tests_properties(configure_current configure_current_self_test configure_guard
        PROPERTIES LABELS fixtures)

# Which mutant files a change makes due to run again (M1-92, ADR 0024, register
# decision 212): the strict rule, that a file is due when anything its judges
# depend on has changed since scripts/mutation-passes.json says it last passed.
# The script is run by hand before a task's commit; here it is held to two
# claims -- its decision function, by its self-test, and its reading of this
# very tree, by assuming core/Scalar.hpp changed and requiring the two mutant
# files that mutate it to be listed. About 8 s.
#
# The four that read the tree are registered only where the application is
# built (M1-100, register decision 246). Several mutant files are judged by the
# application and its tests, and a tree that builds the core only cannot say
# what those depend on: there the script stopped on the first of them, and the
# test that must fail passed by crashing. Mutation passes run in a tree that
# builds everything. The self-test reads no tree, so it runs everywhere.
if(ORBSIM_PYTHON AND ORBSIM_BUILD_APP)
    add_test(NAME mutants_due
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants-due.py
                    ${CMAKE_BINARY_DIR} --assume-changed src/core/Scalar.hpp --only-assumed
                    --expect-due m1-12.json --expect-due m1-87.json
    )
    # The two run-time paths, each alone, because either would otherwise hide a
    # fault in the other: a shader no mutant touches must make the application's
    # judges due (M1-13, through orbsim_smoke), and a script named on a ctest
    # entry's command line must make its file due (M1-15, tonemap_constants).
    add_test(NAME mutants_due_shaders
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants-due.py
                    ${CMAKE_BINARY_DIR} --assume-changed shaders/line.vert --only-assumed
                    --expect-due m1-13.json
    )
    add_test(NAME mutants_due_scripts
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants-due.py
                    ${CMAKE_BINARY_DIR} --assume-changed scripts/check-tonemap-constants.py
                    --only-assumed --expect-due m1-15.json
    )
    # And the expectation itself must be able to fail: a file expected due that
    # is not must fail the run. Added after M1-92's mutation pass found that
    # nothing checked it -- every real run above expects a file that is due.
    add_test(NAME mutants_due_unmet_expectation_fails
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants-due.py
                    ${CMAKE_BINARY_DIR} --assume-changed README.md --only-assumed
                    --expect-unmet m1-12.json
    )
    # The fingerprints that decide what a build-definition change made due
    # (M1-103, register decision 250), taken twice from two fresh readings of
    # this tree and required to agree: one that moved on its own would make
    # files due at random. m1-17.json, because its judges include the
    # application, the largest program. About 10 s.
    add_test(NAME mutants_due_fingerprints
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants-due.py
                    ${CMAKE_BINARY_DIR} --fingerprints m1-17.json
    )
    # --expect-unmet requires the check to fail and to say why, both: CTest's own
    # rules can require one or the other, and each let a defect through -- a
    # refusal or a crash under WILL_FAIL, a check that said so but exited 0
    # under a message rule (M1-102, register decisions 249 and 251).
    # One at a time: each asks Ninja for the tree's header record, and past a
    # size threshold Ninja rewrites that record whenever it is read -- two
    # readers at once on Windows can destroy it (M1-102, register decision 249;
    # parallel_tests requires the lock).
    set_tests_properties(mutants_due mutants_due_shaders mutants_due_scripts mutants_due_fingerprints
            mutants_due_unmet_expectation_fails PROPERTIES LABELS fixtures RESOURCE_LOCK ninja_deps)
elseif(ORBSIM_BUILD_APP)
    add_test(NAME mutants_due COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME mutants_due_shaders COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME mutants_due_scripts COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME mutants_due_unmet_expectation_fails COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME mutants_due_fingerprints COMMAND ${CMAKE_COMMAND} -E false)
    set_tests_properties(mutants_due mutants_due_shaders mutants_due_scripts mutants_due_fingerprints
            mutants_due_unmet_expectation_fails PROPERTIES LABELS fixtures)
endif()
if(ORBSIM_PYTHON)
    add_test(NAME mutants_due_self_test
            COMMAND ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants-due.py --self-test
    )
else()
    add_test(NAME mutants_due_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(mutants_due_self_test PROPERTIES LABELS fixtures)

# AgX is written twice -- shaders/tonemap.frag and src/view/Tonemap.hpp -- and
# nothing reads a pixel back until M1-18 (M1-15, register decision 182). This
# holds the shader's constants to the CPU's, reading GLSL's matrices column by
# column, so a constant copied wrongly, or a matrix transposed, fails `check`
# three tasks before the port check could see it. Milliseconds. A CTest test
# rather than a custom target, so that the mutation pass can name it as the
# judge of a shader mutant: the harness reads a failed build as a mutant that
# does not compile, and a failed test as a kill.
if(ORBSIM_PYTHON)
    add_test(NAME tonemap_constants
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-tonemap-constants.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
    )
else()
    add_test(NAME tonemap_constants COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(tonemap_constants PROPERTIES LABELS fixtures)

# And the check on the check, as a test: the self-test alters copies of the two
# real files and fails unless each alteration is reported, so an edit that
# blinds the script -- a constant renamed, a layout it no longer parses -- fails
# `check` rather than turning it into a comparison of nothing.
if(ORBSIM_PYTHON)
    add_test(NAME tonemap_constants_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-tonemap-constants.py --self-test
    )
else()
    add_test(NAME tonemap_constants_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(tonemap_constants_self_test PROPERTIES LABELS fixtures)

# The memory pool (M1-88, M1-96, ADR 0024): every lint step and every compile
# of this project's own targets in `orbsim_memory`, at the depth the memory rule gives -- read from the Ninja files this tree generated,
# with the depth computed again from the operating system's own figure. And its
# self-test, so that a change which blinds the parser fails `check` rather than
# passing it.
if(ORBSIM_PYTHON)
    add_test(NAME memory_pool
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-memory-pool.py
                    ${CMAKE_BINARY_DIR}
    )
    add_test(NAME memory_pool_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-memory-pool.py --self-test
    )
else()
    add_test(NAME memory_pool COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME memory_pool_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(memory_pool memory_pool_self_test PROPERTIES LABELS fixtures)

# `check` runs the tests in parallel, on half the processor threads (M1-91,
# ADR 0024, register decision 211). Nearly every test is an independent
# program; run one at a time they took 48-52 s in release and 103-113 s in
# Debug (docs/measurements/verification-cost.md). The GPU tests hold one lock
# so they never overlap; the probes write to separate directories and
# test_image_files gives each case its own scratch file. Those scratch names
# are the same in both trees, so the two trees' `check` still run one after
# the other. Checked by the CTest test parallel_tests, which reads CTest's own
# description of the tests and computes the job count itself. It is told
# whether this tree builds the application: where it does, a tree with no GPU
# test would be checking nothing; where it does not -- the core-only Linux
# presets -- a GPU test would be the fault (M1-100, register decision 246).
cmake_host_system_information(RESULT orbsim_logical_cores QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR ORBSIM_TEST_JOBS "${orbsim_logical_cores} / 2")
if(ORBSIM_TEST_JOBS LESS 1)
    set(ORBSIM_TEST_JOBS 1)
endif()
message(STATUS "check: tests run ${ORBSIM_TEST_JOBS} at a time (${orbsim_logical_cores} threads / 2)")
if(ORBSIM_BUILD_APP)
    set(orbsim_gpu_tests expected)
else()
    set(orbsim_gpu_tests none)
endif()
if(ORBSIM_PYTHON)
    add_test(NAME parallel_tests
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-parallel-tests.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND} --gpu-tests ${orbsim_gpu_tests}
    )
    add_test(NAME parallel_tests_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-parallel-tests.py --self-test
    )
else()
    add_test(NAME parallel_tests COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME parallel_tests_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(parallel_tests parallel_tests_self_test PROPERTIES LABELS fixtures)

# No test can accept a golden image (M1-17, ADR 0008, register decision 233):
# --accept-golden is run by the owner, never by a script and never from
# `check`. Read from CTest's own description of the tests, as parallel_tests
# is. Only where the application is built, since only there is a golden
# compared at all -- and the check refuses a tree where nothing passes
# --golden, rather than passing by looking in the wrong place.
if(ORBSIM_PYTHON AND ORBSIM_BUILD_APP)
    add_test(NAME accept_golden
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-accept-golden.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_CTEST_COMMAND}
    )
elseif(ORBSIM_BUILD_APP)
    add_test(NAME accept_golden COMMAND ${CMAKE_COMMAND} -E false)
endif()
if(ORBSIM_PYTHON)
    add_test(NAME accept_golden_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-accept-golden.py --self-test
    )
else()
    add_test(NAME accept_golden_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
if(ORBSIM_BUILD_APP)
    set_tests_properties(accept_golden PROPERTIES LABELS fixtures)
endif()
set_tests_properties(accept_golden_self_test PROPERTIES LABELS fixtures)

# The downsample reference's own check (register decision 301): its average of
# four values is held to hand-worked results, exact ties on the sRGB curve's
# straight segment first, which it rounded the wrong way until 2026-10-03.
# Nothing else runs the script -- its output was pasted into
# tests/test_image_compare.cpp -- so without this its fix would be an intention.
if(ORBSIM_PYTHON)
    add_test(NAME downsample_reference_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/downsample-reference.py --self-test
    )
else()
    add_test(NAME downsample_reference_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(downsample_reference_self_test PROPERTIES LABELS fixtures)

# The abort listener in every suite (M1-90, ADR 0024): each Catch2 program is
# asked through --list-listeners, and each must name AbortWithoutADialog. A
# suite that stopped linking orbsim_abort_behaviour would otherwise build and
# pass, and only a failed assertion under a debugger would show the dialog.
set(orbsim_listener_programs "")
foreach(test_name IN LISTS ORBSIM_TESTS)
    list(APPEND orbsim_listener_programs $<TARGET_FILE:${test_name}>)
endforeach()
if(ORBSIM_BUILD_APP)
    list(APPEND orbsim_listener_programs $<TARGET_FILE:test_probe_clear>
            $<TARGET_FILE:test_radiometry> $<TARGET_FILE:test_tonemap_port>)
endif()
if(ORBSIM_PYTHON)
    add_test(NAME abort_listener
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-abort-listener.py
                    ${orbsim_listener_programs}
    )
    add_test(NAME abort_listener_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-abort-listener.py --self-test
    )
else()
    add_test(NAME abort_listener COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME abort_listener_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(abort_listener abort_listener_self_test PROPERTIES LABELS fixtures)

# The lint steps' dependencies (M1-89, ADR 0024): each on its own objects, the
# configurations, a verify stamp and clang-tidy, and on no project header
# directly; and no `__clang_analyzer__` anywhere in src/ or tests/. Read from
# the Ninja file and the compile database this tree generated. And its
# self-test, which feeds it each fault it exists to catch.
if(ORBSIM_PYTHON)
    add_test(NAME lint_deps
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-lint-deps.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME lint_deps_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-lint-deps.py --self-test
    )
else()
    add_test(NAME lint_deps COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME lint_deps_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(lint_deps lint_deps_self_test PROPERTIES LABELS fixtures)
