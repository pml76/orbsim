cmake_minimum_required(VERSION 3.24)
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
# Simulation core
#
# Deliberately free of any dependency on the renderer or on SDL: the physics
# must be runnable headless, both for tests and, later, for a scenario
# batch-runner.
# ---------------------------------------------------------------------------
add_library(orbsim_core STATIC
        src/orbit/Orbit.cpp
)
target_include_directories(orbsim_core PUBLIC src)

if(MSVC)
    target_compile_options(orbsim_core PRIVATE /W4)
else()
    # Contracting doubles into FMAs reorders arithmetic, which quietly breaks
    # the round-trip identities the orbital code relies on. Keep it off.
    target_compile_options(orbsim_core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)

add_executable(test_orbit tests/test_orbit.cpp)
target_link_libraries(test_orbit PRIVATE orbsim_core)
add_test(NAME orbit COMMAND test_orbit)

# ---------------------------------------------------------------------------
# Graphics dependencies
#
# Vulkan headers are fetched at a pinned version rather than taken from
# whatever SDK happens to be installed, so the build is reproducible across
# machines. Only the loader library and the shader compiler come from the SDK.
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED COMPONENTS glslc)

FetchContent_Declare(vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.302
        GIT_SHALLOW    TRUE
)

FetchContent_Declare(vk_bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG        v1.3.302
        GIT_SHALLOW    TRUE
)

FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.2.1
        GIT_SHALLOW    TRUE
)

set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.16
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(vulkan_headers vk_bootstrap vma sdl3)

# VMA ships as a single header; this target holds the one translation unit that
# instantiates its implementation.
add_library(vma_impl STATIC src/render/VmaImpl.cpp)
target_link_libraries(vma_impl PUBLIC Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(vma_impl PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
# VMA's own header is not warning-clean under -Wall; that is not our problem to fix.
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

if(NOT MSVC)
    # Vulkan structs are written as designated initialisers with the remaining
    # fields intentionally value-initialised to zero, which is the idiomatic
    # style. Warning on each one buries anything real.
    target_compile_options(orbsim PRIVATE -Wall -Wextra -Wno-missing-designated-field-initializers)
endif()

# SDL3 is built as a DLL; put it next to the executable so the app runs from
# the build tree without any PATH fiddling.
add_custom_command(TARGET orbsim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:orbsim>
        VERBATIM
)
