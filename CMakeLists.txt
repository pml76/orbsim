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
# AddressSanitizer, off by default and one flag away.
#
# UBSan is deliberately not offered here: its Windows support is partial, and a
# build option that silently does less than its name promises is worse than no
# option. On a Linux job, use -fsanitize=address,undefined.
# ---------------------------------------------------------------------------
option(ORBSIM_SANITIZE_ADDRESS "Build with AddressSanitizer" OFF)
add_library(orbsim_sanitizers INTERFACE)
if(ORBSIM_SANITIZE_ADDRESS)
    target_compile_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=address -fno-omit-frame-pointer>)
    target_link_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=address>)
endif()

# ---------------------------------------------------------------------------
# Simulation core
#
# Deliberately free of any dependency on the renderer or on SDL: the physics
# must run headless, both for tests and for a future scenario batch-runner.
# ---------------------------------------------------------------------------
add_library(orbsim_core STATIC
        src/orbit/Orbit.cpp
)
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
# Graphics dependencies
#
# Vulkan headers are fetched at a pinned version rather than taken from
# whatever SDK happens to be installed, so the build is reproducible across
# machines. Only the loader library and the shader compiler come from the SDK.
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
add_executable(orbsim
        src/app/main.cpp
        src/render/VulkanContext.cpp
)
add_dependencies(orbsim orbsim_shaders)

target_link_libraries(orbsim PRIVATE
        orbsim_core
        orbsim_warnings
        orbsim_sanitizers
        vma_impl
        vk-bootstrap::vk-bootstrap
        Vulkan::Headers
        SDL3::SDL3
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
