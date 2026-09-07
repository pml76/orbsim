cmake_minimum_required(VERSION 3.25)
project(orbsim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# Warnings, as errors.
#
# See CODING_GUIDELINES.md section 1. Carried on an INTERFACE target so that
# every target below gets exactly the same set and none can quietly opt out.
# Third-party headers are included with -isystem (see the SYSTEM argument on
# the FetchContent_Declare calls), so this only ever reports our own code.
# ---------------------------------------------------------------------------
add_library(orbsim_warnings INTERFACE)
target_compile_options(orbsim_warnings INTERFACE
        $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:
            -Wall -Wextra -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            # Vulkan structs are written as designated initialisers with the
            # remaining fields intentionally value-initialised. Warning on each
            # one buries everything real.
            -Wno-missing-designated-field-initializers
            -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive->
)

# ---------------------------------------------------------------------------
# Floating point. See CODING_GUIDELINES.md section 11.
#
# FMA contraction reorders arithmetic, and reordered arithmetic breaks the
# round-trip identities the orbital tests depend on. -ffast-math is never set,
# at any optimisation level: orbitInfo() returns infinity on purpose.
#
# PUBLIC on orbsim_core below, so anything computing against the physics
# inherits the same arithmetic.
# ---------------------------------------------------------------------------
add_library(orbsim_fp INTERFACE)
target_compile_options(orbsim_fp INTERFACE
        $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ffp-contract=off>
        $<$<CXX_COMPILER_ID:MSVC>:/fp:precise>
)

# ---------------------------------------------------------------------------
# Sanitizers, off by default and one flag each away. See the asan and
# linux-sanitize presets.
#
# On Windows, ASan and the MSVC *debug* C runtime (/MDd, what a Debug build
# links) do not coexist: the debug heap frees blocks ASan never allocated, and
# every sanitized executable dies at startup with "bad-free" inside
# ucrtbased.dll before reaching main. The asan preset therefore builds
# RelWithDebInfo with NDEBUG removed -- release runtime, optimiser on,
# assertions live -- which is the combination that works.
#
# UBSan refuses to configure on Windows rather than quietly doing less than
# its name promises: its support there is partial, and a build option that
# silently under-delivers is worse than no option. The Linux CI job is where
# it runs.
# ---------------------------------------------------------------------------
option(ORBSIM_SANITIZE_ADDRESS "Build with AddressSanitizer" OFF)
option(ORBSIM_SANITIZE_UNDEFINED "Build with UndefinedBehaviorSanitizer (not on Windows)" OFF)
add_library(orbsim_sanitizers INTERFACE)
if(ORBSIM_SANITIZE_ADDRESS)
    target_compile_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=address -fno-omit-frame-pointer>)
    target_link_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=address>)
    if(WIN32)
        # The MSVC STL annotates std::string and std::vector for ASan and
        # refuses (LNK2038 / lld "failifmismatch: annotate_string") to link an
        # instrumented object against one that is not. vk-bootstrap is built
        # without the sanitizer, so the annotations are switched off here.
        # What is lost is overflow detection *inside* those two containers'
        # spare capacity; heap, stack and use-after-free detection are intact.
        target_compile_definitions(orbsim_sanitizers INTERFACE
                _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)

        # clang's ASan on Windows links against a runtime DLL that lives in the
        # compiler's own tree, not on PATH. Without it every sanitized
        # executable dies at startup with STATUS_DLL_NOT_FOUND (0xc0000135)
        # before printing anything, which is a miserable way to learn this.
        # The build tree is where the executables are, so the DLL goes there.
        execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-runtime-dir
                OUTPUT_VARIABLE orbsim_runtime_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE orbsim_runtime_dir_result
        )
        file(GLOB orbsim_asan_dlls "${orbsim_runtime_dir}/clang_rt.asan_dynamic-*.dll")
        if(NOT orbsim_runtime_dir_result EQUAL 0 OR NOT orbsim_asan_dlls)
            message(FATAL_ERROR "ORBSIM_SANITIZE_ADDRESS: no clang_rt.asan_dynamic-*.dll under "
                    "'${orbsim_runtime_dir}' (from '${CMAKE_CXX_COMPILER} -print-runtime-dir').")
        endif()
        file(COPY ${orbsim_asan_dlls} DESTINATION ${CMAKE_BINARY_DIR})
    endif()
endif()
if(ORBSIM_SANITIZE_UNDEFINED)
    if(WIN32)
        message(FATAL_ERROR "ORBSIM_SANITIZE_UNDEFINED: UBSan has only partial support on Windows. "
                "Use a Linux build (the linux-sanitize preset) rather than a build that checks less than it claims.")
    endif()
    # -fno-sanitize-recover: a UB report is a test failure, not a log line.
    target_compile_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer>)
    target_link_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=undefined>)
endif()

# The application needs the Vulkan SDK for the loader and glslc, which a
# headless CI runner does not have. The core and its tests never did (section
# 12), and this option is what makes that claim checkable.
option(ORBSIM_BUILD_APP "Build the Vulkan renderer and application" ON)

# ---------------------------------------------------------------------------
# Simulation core
#
# Deliberately free of any dependency on the renderer or on SDL: the physics
# must run headless, both for tests and for a future scenario batch-runner.
# ---------------------------------------------------------------------------
set(ORBSIM_CORE_SOURCES
        src/orbit/Orbit.cpp
)
add_library(orbsim_core STATIC ${ORBSIM_CORE_SOURCES})
target_include_directories(orbsim_core PUBLIC src)
target_link_libraries(orbsim_core
        PUBLIC orbsim_fp
        PRIVATE orbsim_warnings orbsim_sanitizers
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)

# The harness and the fixtures are headers under tests/, included from the
# repository root as "tests/TestHarness.hpp" so a test reads the same way as a
# source file does.
add_library(orbsim_test_support INTERFACE)
target_include_directories(orbsim_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orbsim_test_support INTERFACE orbsim_core orbsim_warnings orbsim_sanitizers)

set(ORBSIM_TESTS
        test_orbit
        test_orbit_scales
)
foreach(test_name IN LISTS ORBSIM_TESTS)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE orbsim_test_support)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---------------------------------------------------------------------------
# Fuzzing. See VERIFICATION.md rule 13 and tests/fuzz_orbit.cpp.
#
# Off by default and not a CTest test: a fuzzer has no natural exit, so it is
# run deliberately with a time or run budget rather than as part of `check`.
# clang's libFuzzer does not target the MSVC ABI, so this is a Linux/WSL
# target; the option refuses to configure elsewhere rather than producing a
# build that cannot link.
# ---------------------------------------------------------------------------
option(ORBSIM_BUILD_FUZZERS "Build the libFuzzer targets (Linux/WSL only)" OFF)
if(ORBSIM_BUILD_FUZZERS)
    if(WIN32)
        message(FATAL_ERROR "ORBSIM_BUILD_FUZZERS: clang's libFuzzer does not target the MSVC ABI. "
                "Use the linux-fuzz preset under WSL.")
    endif()
    add_executable(fuzz_orbit tests/fuzz_orbit.cpp)
    target_link_libraries(fuzz_orbit PRIVATE orbsim_core orbsim_warnings)
    # The sanitizers come with the fuzzer here rather than through
    # orbsim_sanitizers, so that a fuzz build is always instrumented: an
    # uninstrumented fuzzer finds only the crashes that would have crashed
    # anyway, which is the smallest and least interesting class.
    target_compile_options(fuzz_orbit PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(fuzz_orbit PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(ORBSIM_BUILD_APP)

# ---------------------------------------------------------------------------
# Graphics dependencies
#
# Vulkan headers are fetched at a pinned version rather than taken from
# whatever SDK happens to be installed, so the build is reproducible across
# machines. Only the loader library and the shader compiler come from the SDK.
# See docs/adr/0004.
#
# SYSTEM on every dependency: their headers are not our code, and 640 warnings
# from vk_mem_alloc.h would bury the one that matters.
# ---------------------------------------------------------------------------
FetchContent_Declare(vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.302
        GIT_SHALLOW    TRUE
        SYSTEM
)

FetchContent_Declare(vk_bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG        v1.3.302
        GIT_SHALLOW    TRUE
        SYSTEM
)

FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.2.1
        GIT_SHALLOW    TRUE
        SYSTEM
)

set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.16
        GIT_SHALLOW    TRUE
        SYSTEM
)

# Vulkan-Headers first, and on its own: it defines the Vulkan::Headers target,
# and FindVulkan below will not overwrite a target that already exists. That
# ordering is what makes "pinned headers, SDK only for the loader and glslc"
# actually true rather than merely intended -- without it, an SDK upgrade on the
# build machine silently changes which headers this project compiles against.
FetchContent_MakeAvailable(vulkan_headers)
FetchContent_MakeAvailable(vk_bootstrap vma sdl3)

# The SDK supplies only the loader import library and the shader compiler.
find_package(Vulkan REQUIRED COMPONENTS glslc)

# VMA ships as a single header; this target holds the one translation unit that
# instantiates its implementation.
add_library(vma_impl STATIC src/render/VmaImpl.cpp)
target_link_libraries(vma_impl PUBLIC Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(vma_impl PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
# VMA's own header is not warning-clean; that is not ours to fix, and it is not
# ours to hear about either.
if(NOT MSVC)
    target_compile_options(vma_impl PRIVATE -Wno-everything)
endif()

# ---------------------------------------------------------------------------
# Shaders
#
# GLSL is compiled to SPIR-V at build time and tracked as a real dependency, so
# editing a shader triggers a rebuild the same way editing a .cpp does.
# ---------------------------------------------------------------------------
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

set(SHADER_SOURCES
        shaders/line.vert
        shaders/line.frag
        shaders/body.vert
        shaders/body.frag
)

set(SPIRV_OUTPUTS "")
foreach(shader ${SHADER_SOURCES})
    get_filename_component(shader_name ${shader} NAME)
    set(spv ${SHADER_OUT_DIR}/${shader_name}.spv)
    add_custom_command(
            OUTPUT ${spv}
            COMMAND Vulkan::glslc --target-env=vulkan1.3 -O
                    ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${spv}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
            COMMENT "glslc ${shader_name}"
            VERBATIM
    )
    list(APPEND SPIRV_OUTPUTS ${spv})
endforeach()

add_custom_target(orbsim_shaders DEPENDS ${SPIRV_OUTPUTS})

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
# What the renderer and the app see: the core, plus the graphics libraries.
# One target, so that the header self-check below compiles those headers with
# exactly the view of the world the application has.
add_library(orbsim_render_deps INTERFACE)
target_link_libraries(orbsim_render_deps INTERFACE
        orbsim_core
        vma_impl
        vk-bootstrap::vk-bootstrap
        Vulkan::Headers
        SDL3::SDL3
)

set(ORBSIM_APP_SOURCES
        src/app/main.cpp
        src/render/VulkanContext.cpp
)
add_executable(orbsim ${ORBSIM_APP_SOURCES})
add_dependencies(orbsim orbsim_shaders)

target_link_libraries(orbsim PRIVATE
        orbsim_render_deps
        orbsim_warnings
        orbsim_sanitizers
        ${Vulkan_LIBRARY}
)

target_compile_definitions(orbsim PRIVATE
        ORBSIM_SHADER_DIR="${SHADER_OUT_DIR}"
        ORBSIM_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

# SDL3 is built as a DLL; put it next to the executable so the app runs from
# the build tree without any PATH fiddling.
add_custom_command(TARGET orbsim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:orbsim>
        VERBATIM
)

# Runs the application under the validation layers for two seconds: startup,
# the frame loop, and teardown, which is where validation errors hide. A
# validation error makes it exit non-zero, so on a machine with a GPU this is
# the renderer's regression test. Labelled so that CI, which has no GPU, can
# leave it out with --label-exclude gpu.
add_test(NAME orbsim_smoke COMMAND orbsim --validate --seconds 2)
set_tests_properties(orbsim_smoke PROPERTIES LABELS gpu)

endif() # ORBSIM_BUILD_APP

# ---------------------------------------------------------------------------
# Header self-containment (SF.11), enforced rather than assumed.
#
# One generated translation unit per header, containing nothing but that
# include. If a header stops compiling on its own, the build stops.
# ---------------------------------------------------------------------------
function(orbsim_add_header_selfcheck target link_target)
    set(sources "")
    foreach(header IN LISTS ARGN)
        string(REPLACE "/" "_" stem "${header}")
        string(REPLACE "." "_" stem "${stem}")
        set(generated "${CMAKE_CURRENT_BINARY_DIR}/selfcheck/${target}_${stem}.cpp")
        file(WRITE "${generated}"
                "// Generated by CMake. Proves ${header} is self-contained (SF.11).\n"
                "#include \"${header}\"\n")
        list(APPEND sources "${generated}")
    endforeach()
    add_library(${target} OBJECT ${sources})
    target_link_libraries(${target} PRIVATE ${link_target} orbsim_warnings)
endfunction()

orbsim_add_header_selfcheck(orbsim_header_selfcheck orbsim_core
        core/Contract.hpp
        core/Scalar.hpp
        core/Units.hpp
        core/Math.hpp
        orbit/Orbit.hpp
)
orbsim_add_header_selfcheck(orbsim_test_header_selfcheck orbsim_test_support
        tests/TestHarness.hpp
        tests/OrbitTestSupport.hpp
)
add_dependencies(test_orbit orbsim_header_selfcheck orbsim_test_header_selfcheck)

if(ORBSIM_BUILD_APP)
    orbsim_add_header_selfcheck(orbsim_render_header_selfcheck orbsim_render_deps
            render/VulkanHandle.hpp
            render/VulkanContext.hpp
            app/SdlHandle.hpp
    )
    add_dependencies(orbsim orbsim_render_header_selfcheck)
endif()

# ---------------------------------------------------------------------------
# The definition of done.
#
# `cmake --build <tree> --target check` is what "finished" means here:
# everything builds, every test passes, clang-tidy finds nothing, and
# clang-format would change nothing. CLAUDE.md points at this target. Run it
# in both build/relwithdebinfo and build/debug -- the assertions are only
# live in the second.
#
# lint is one stamp per translation unit, so Ninja runs clang-tidy in
# parallel and re-lints only what changed. Every header is a dependency of
# every stamp, which is correct (a header change can surface anywhere) and
# still far cheaper than the serial run it replaces.
# ---------------------------------------------------------------------------
find_program(ORBSIM_CLANG_TIDY clang-tidy)
find_program(ORBSIM_CLANG_FORMAT clang-format)

set(ORBSIM_LINT_SOURCES ${ORBSIM_CORE_SOURCES})
foreach(test_name IN LISTS ORBSIM_TESTS)
    list(APPEND ORBSIM_LINT_SOURCES tests/${test_name}.cpp)
endforeach()
if(ORBSIM_BUILD_APP)
    list(APPEND ORBSIM_LINT_SOURCES ${ORBSIM_APP_SOURCES})
endif()

# Formatting covers everything that is ours, whether or not it is built in
# this tree; VmaImpl.cpp is excluded because its one include is third-party.
file(GLOB_RECURSE ORBSIM_OWN_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp
)
file(GLOB_RECURSE ORBSIM_OWN_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
)
list(FILTER ORBSIM_OWN_SOURCES EXCLUDE REGEX "VmaImpl\\.cpp$")

if(ORBSIM_CLANG_TIDY)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lint)
    set(orbsim_lint_stamps "")
    foreach(source IN LISTS ORBSIM_LINT_SOURCES)
        string(REPLACE "/" "_" stem "${source}")
        set(stamp "${CMAKE_CURRENT_BINARY_DIR}/lint/${stem}.ok")
        add_custom_command(
                OUTPUT ${stamp}
                COMMAND ${ORBSIM_CLANG_TIDY} --quiet -p ${CMAKE_BINARY_DIR}
                        ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                        ${ORBSIM_OWN_HEADERS}
                        ${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy
                COMMENT "clang-tidy ${source}"
                VERBATIM
        )
        list(APPEND orbsim_lint_stamps ${stamp})
    endforeach()
    add_custom_target(lint DEPENDS ${orbsim_lint_stamps})
else()
    # A lint target that cannot lint must fail, not pass by doing nothing.
    add_custom_target(lint
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "clang-tidy was not found on PATH; lint cannot run"
            VERBATIM
    )
endif()

if(ORBSIM_CLANG_FORMAT)
    add_custom_target(format-check
            COMMAND ${ORBSIM_CLANG_FORMAT} --dry-run --Werror ${ORBSIM_OWN_SOURCES} ${ORBSIM_OWN_HEADERS}
            COMMENT "clang-format: checking every source and header"
            VERBATIM
    )
    add_custom_target(format
            COMMAND ${ORBSIM_CLANG_FORMAT} -i ${ORBSIM_OWN_SOURCES} ${ORBSIM_OWN_HEADERS}
            COMMENT "clang-format: formatting every source and header"
            VERBATIM
    )
else()
    add_custom_target(format-check
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "clang-format was not found on PATH; format-check cannot run"
            VERBATIM
    )
endif()

add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "ctest: every suite, and the GPU smoke test where there is a GPU"
        VERBATIM
)
add_dependencies(check
        ${ORBSIM_TESTS}
        orbsim_header_selfcheck
        orbsim_test_header_selfcheck
        lint
        format-check
)
if(ORBSIM_BUILD_APP)
    add_dependencies(check orbsim orbsim_render_header_selfcheck)
endif()
