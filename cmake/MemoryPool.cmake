# The memory pool: its size, the lint steps that run in it, and the function
# that puts this project's own compiles into it.
#
# Moved out of CMakeLists.txt by M1-105 (register decision 254). The pool and
# the lint steps are included where they stood, unchanged, in the same
# directory scope. The compile half has to run once every target exists, so it
# became orbsim_pool_top_level_compiles, at the end of this file, which
# CMakeLists.txt calls last -- where the same loop stood before. It has a file
# of its own so that the mutants aimed at it -- scripts/mutants/m1-88.json and
# m1-96.json -- are due again when this file changes, and not on every edit to
# CMakeLists.txt. Registered with the configure guard (M1-98) before it is
# included.

# How many heavy processes -- clang-tidy, and this project's own compiles -- may
# run at once (M1-88 and M1-96, ADR 0024, register decisions 208 and 222). Each
# clang-tidy needs about 1.04 GB on this project -- mp-units is most of
# it -- and Ninja's default, one job per thread plus two, asked for about 22 GB
# on a 32 GB machine: it ran the lint step out of memory beside an open IDE on
# 2026-09-27 (docs/measurements/verification-cost.md). So the lint steps share a
# pool sized from the machine's physical memory, one job per 3 GiB -- a third of
# which the process needs, the rest headroom for the IDE and the compiler -- and
# at least one. 10 on the 32 GB development machine, measured safe there.
# Since M1-96 this project's own compiles share the pool, 0.6-1 GB each: a full
# rebuild ran `check` out of memory on 2026-09-27 with about 20 compiles beside
# the 10 lint jobs. orbsim_pool_top_level_compiles, at the end of this file and
# called last in CMakeLists.txt, puts every library and program defined there
# into it; dependencies built in their own directories
# keep Ninja's default. Checked by the CTest test `memory_pool`, which reads the
# generated Ninja files and computes the depth itself.
cmake_host_system_information(RESULT orbsim_physical_memory_mib QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR ORBSIM_MEMORY_JOBS "${orbsim_physical_memory_mib} / 3072")
if(ORBSIM_MEMORY_JOBS LESS 1)
    set(ORBSIM_MEMORY_JOBS 1)
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS orbsim_memory=${ORBSIM_MEMORY_JOBS})
message(STATUS "memory pool: at most ${ORBSIM_MEMORY_JOBS} compiles and clang-tidy processes at once "
               "(${orbsim_physical_memory_mib} MiB / 3 GiB)")

if(ORBSIM_CLANG_TIDY)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lint)

    # The check on the check, and it runs before them.
    #
    # `--verify-config` exits non-zero on a check name clang-tidy does not
    # know, and a normal run ignores one in silence. Three things look exactly
    # like that: a typo, a check renamed by a compiler upgrade -- which this
    # project meets on purpose, since `.clang-tidy` lists families and every
    # upgrade is a small triage -- and a `#` written inside the `Checks:`
    # block, which is a folded YAML scalar, so the comment folds into the next
    # check name and **silently cancels the suppression it was attached to**.
    # Without this target all three are indistinguishable from a clean run.
    #
    # Shown to fail before it was trusted: a deliberate
    # `-readability-identifier-lenght` exits 1 and names the typo.
    #
    # Once per directory whose files are linted, because clang-tidy resolves a
    # config per file and a `tests/.clang-tidy` would be a second one. 88 ms
    # each. The glob is CONFIGURE_DEPENDS over exactly those directories, so a
    # per-directory config added later joins this dependency list by itself
    # rather than by somebody remembering.
    set(orbsim_lint_dirs "")
    set(orbsim_lint_dir_sources "")
    foreach(source IN LISTS ORBSIM_LINT_SOURCES)
        get_filename_component(orbsim_lint_dir "${source}" DIRECTORY)
        if(NOT "${orbsim_lint_dir}" IN_LIST orbsim_lint_dirs)
            list(APPEND orbsim_lint_dirs "${orbsim_lint_dir}")
            list(APPEND orbsim_lint_dir_sources "${source}")
        endif()
    endforeach()

    set(orbsim_tidy_config_globs ${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy)
    foreach(dir IN LISTS orbsim_lint_dirs)
        list(APPEND orbsim_tidy_config_globs
                ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/.clang-tidy)
    endforeach()
    file(GLOB ORBSIM_TIDY_CONFIGS CONFIGURE_DEPENDS ${orbsim_tidy_config_globs})

    set(orbsim_verify_stamps "")
    foreach(source IN LISTS orbsim_lint_dir_sources)
        get_filename_component(orbsim_lint_dir "${source}" DIRECTORY)
        string(REPLACE "/" "_" stem "${orbsim_lint_dir}")
        set(stamp "${CMAKE_CURRENT_BINARY_DIR}/lint/verify_${stem}.ok")
        add_custom_command(
                OUTPUT ${stamp}
                COMMAND ${ORBSIM_CLANG_TIDY} --verify-config -p ${CMAKE_BINARY_DIR}
                        ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
                DEPENDS ${ORBSIM_TIDY_CONFIGS}
                COMMENT "clang-tidy --verify-config for ${orbsim_lint_dir}/"
                JOB_POOL orbsim_memory
                VERBATIM
        )
        list(APPEND orbsim_verify_stamps ${stamp})
    endforeach()

    # A file is re-linted exactly when the text clang-tidy reads for it can
    # have changed (M1-89, ADR 0024, register decisions 209 and 217). Each lint
    # step depends on the file's own compiled object -- in every target that
    # compiles it, since each can see different flags -- and Ninja rebuilds an
    # object exactly when the file or anything it includes changes: it records
    # every header the compiler reports, system headers too. Until 2026-09-27
    # every step depended on every project header instead, so any header edit
    # re-linted all 48 files, 206-313 s per tree. The configurations, the
    # verify stamps and clang-tidy itself stay inputs, so changing any of them
    # still re-lints everything. The CTest test `lint_deps` checks all of it
    # against the generated Ninja file and the compile database, and refuses
    # `__clang_analyzer__`, the one way the linter could read text the
    # compiler does not.
    set(orbsim_lint_targets
            orbsim_core orbsim_view orbsim_stb_image orbsim_test_support orbsim_fuzz_objects
            orbsim_abort_behaviour ${ORBSIM_TESTS})
    foreach(operation IN LISTS ORBSIM_COUNT_WRAPAROUND_OPERATIONS)
        list(APPEND orbsim_lint_targets count_wraparound_${operation})
    endforeach()
    if(ORBSIM_BUILD_APP)
        list(APPEND orbsim_lint_targets orbsim test_probe_clear make_broken_goldens)
    endif()

    set(orbsim_lint_stamps "")
    foreach(source IN LISTS ORBSIM_LINT_SOURCES)
        string(REPLACE "/" "_" stem "${source}")
        set(stamp "${CMAKE_CURRENT_BINARY_DIR}/lint/${stem}.ok")
        # The paths here hold only letters, digits, '_', '/' and '.', so
        # escaping the dots is all the regular expression needs.
        string(REPLACE "." "[.]" source_pattern "${source}")
        set(source_objects "")
        foreach(target IN LISTS orbsim_lint_targets)
            get_target_property(target_sources ${target} SOURCES)
            if("${source}" IN_LIST target_sources
                    OR "${CMAKE_CURRENT_SOURCE_DIR}/${source}" IN_LIST target_sources)
                list(APPEND source_objects
                        "$<FILTER:$<TARGET_OBJECTS:${target}>,INCLUDE,/${source_pattern}[.]o(bj)?$>")
            endif()
        endforeach()
        if(NOT source_objects)
            message(FATAL_ERROR "lint: no target compiles ${source}, so its lint step "
                                "would have nothing to re-run on; add the target to "
                                "orbsim_lint_targets")
        endif()
        add_custom_command(
                OUTPUT ${stamp}
                COMMAND ${ORBSIM_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR}
                        ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                        ${source_objects}
                        ${ORBSIM_CLANG_TIDY}
                        ${ORBSIM_TIDY_CONFIGS}
                        ${orbsim_verify_stamps}
                COMMENT "clang-tidy ${source}"
                JOB_POOL orbsim_memory
                VERBATIM
        )
        list(APPEND orbsim_lint_stamps ${stamp})
    endforeach()
    add_custom_target(lint DEPENDS ${orbsim_verify_stamps} ${orbsim_lint_stamps})
else()
    # A lint target that cannot lint must fail, not pass by doing nothing.
    add_custom_target(lint
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "clang-tidy was not found on PATH; lint cannot run"
            VERBATIM
    )
endif()

# Every library and program CMakeLists.txt defines compiles in the memory pool
# (M1-96, register decision 222), beside the lint steps: together they never run
# more heavy processes than the machine's memory holds. CMakeLists.txt calls this
# last, so that every target exists -- the top-level directory's targets, those
# its included files define among them. Targets a dependency defines in its own directory
# are not listed here and keep Ninja's default -- they are small, and pooling
# them would slow a fresh build for no memory gain.
function(orbsim_pool_top_level_compiles)
    get_property(orbsim_top_level_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS orbsim_top_level_targets)
        get_target_property(target_type ${target} TYPE)
        if(target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE orbsim_memory)
        endif()
    endforeach()
endfunction()
