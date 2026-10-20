# 3.28 for EXCLUDE_FROM_ALL on FetchContent_Declare, which Vulkan-Utility-
# Libraries needs: its src/ unconditionally builds two static libraries this
# project never links -- VulkanLayerSettings and VulkanSafeStruct, 76,081 lines
# of generated C++ measured at v1.3.302 -- and without EXCLUDE_FROM_ALL every
# `cmake --build` in every tree compiles them. Raised from 3.25 on 2026-09-16;
# nothing here is near the floor (4.3.1 on Windows, 4.2.3 in WSL).
cmake_minimum_required(VERSION 3.28)

# Fingerprint this file as soon as it has been read -- before project(), which
# is what takes seconds -- so that an edit landing during the configure fails
# it rather than leaving a build plan made from text no longer on disk (M1-98,
# register decision 225). cmake/ConfigureInputs.cmake says how, and what is
# left: the milliseconds between CMake reading this file and this line.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConfigureInputs.cmake)
orbsim_configure_input("${CMAKE_CURRENT_LIST_FILE}")
orbsim_configure_input("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ConfigureInputs.cmake")

project(orbsim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# No C++20 modules here, so do not scan for them.
#
# This is not housekeeping: raising cmake_minimum_required to 3.28 above
# activated CMP0155, which turns module dependency scanning on by default for
# C++20 and later, and that **broke `lint`**. Scanning puts an `@....modmap`
# argument into every compile command, the compile database hands that argument
# to clang-tidy, and the file does not exist until the object is built -- so
# clang-tidy failed with "no such file or directory: '@...modmap'" on a tree
# that had just been configured. Measured on 2026-09-16: 21 entries in
# compile_commands.json carried the flag and 21 scan steps appeared in the
# graph, where before the bump there were none.
#
# Switching it off restores the lint, removes 21 build steps, and costs nothing
# this project uses. The day a module appears, this line is what to delete --
# and `lint` will need the object built before it runs.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Python, for the scripts that check this project and its build. Looked up
# here, before anything uses it: until 2026-10-12 it was looked up beside
# doc-links, eight hundred lines below the first test that runs a script, so a
# tree's first configure registered float_equal_guard and its self-test as
# `cmake -E false`, and every new tree failed its first `check` -- measured in
# a fresh linux-gcc tree, both tests failing until a second configure found
# the cached value. Every user tests it and fails loudly when it is empty.
find_program(ORBSIM_PYTHON NAMES python3 python)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Debug libraries carry `_d`, from the first configure on (M1-109, register
# decision 395). Imath writes `CMAKE_DEBUG_POSTFIX "_d"` into the cache
# (config/ImathSetup.cmake, v3.2.3), so until this line every target declared
# above OpenEXR's fetch got the suffix only from a tree's *second* configure:
# measured on 2026-10-05, a fresh Debug tree named `orbsim_core.lib` and
# configured once more named it `orbsim_core_d.lib` -- the same commit made
# two build plans, and every Debug program's fingerprint in
# scripts/mutants-due.py differed between a new tree and an old one. A normal
# variable, which a cache entry set later cannot replace (CMP0126); OpenEXR
# keeps a postfix that is already defined. `_d`, the owner's choice: what
# every tree already settled on, and the one the dependencies use.
set(CMAKE_DEBUG_POSTFIX "_d")

# ---------------------------------------------------------------------------
# The processor this project assumes (ADR 0023, register decisions 200-201).
#
# x86-64 with MMX, SSE, SSE2, SSE3, SSSE3, SSE4.1, SSE4.2, FMA3, AVX, AVX2,
# F16C and BMI2 -- all of which this machine's Intel Core i9-12900H and AMD's
# Zen 2 have (decision 202 added F16C, the hardware conversion between 16- and
# 32-bit floats; decision 203 added BMI2, bit-manipulation instructions,
# because zstd inside OpenEXR assumes them wherever a compiler presents itself
# as Microsoft's with AVX2 on). Set for **every file compiled** -- ours and every
# dependency's, which is why
# this comes before the first target and the first FetchContent: a directory's
# compile options reach the subprojects added below it. The owner's list also
# named SSE4A, which is AMD's alone and absent from this machine's Intel Core
# i9-12900H, so a build allowed to emit it could stop with an illegal
# instruction here; "MMX-plus" is AMD's name for additions Intel ships inside
# SSE, and has no flag of its own. MSVC has one switch for the whole set.
#
# **-ffp-contract=off goes everywhere with them, not only on orbsim_fp.** FMA3
# lets the compiler fuse `a * b + c` into one instruction with one rounding,
# and clang does so by default once -mfma is on (measured 2026-09-26: 0 fused
# without it, 1 with it). Our own targets always forbade that; third-party
# code -- Catch2's WithinAbs and WithinRel among it -- did not, and would
# otherwise have changed its arithmetic with this flag. MSVC 19.51 does not
# fuse under /fp:precise unless /fp:contract is given (measured the same day),
# so /fp:precise is set for everything too, which is its default made explicit.
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
            -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mfma -mavx -mavx2 -mf16c -mbmi2
            -ffp-contract=off)
elseif(MSVC)
    add_compile_options(/arch:AVX2 /fp:precise)
endif()

# **Colour in clang's messages off, explicitly, for every compile** -- ours
# and every dependency's, so here, before the first FetchContent (M1-131,
# register decision 691). Left to itself, clang adds -fno-color-diagnostics to
# its own command when NO_COLOR is set in the environment, and keeps that
# command in each object's debug information (.debug$T): the same compile gave
# other bytes from a shell that sets NO_COLOR than from one that does not, and
# reproducible_build failed whenever the two met (measured 2026-10-18). Named
# either way, the bytes are the same with and without it; off keeps colour
# codes out of the logs. A compile option, because CMAKE_COLOR_DIAGNOSTICS set
# here reached no compile at all (measured the same day). SDL3 switches colour
# on for its own targets after this (its cmake/sdlcompilers.cmake), so its
# objects end on -fcolor-diagnostics: explicit too, and as reproducible.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX,ASM>:-fno-color-diagnostics>)
endif()

# ---------------------------------------------------------------------------
# The same source makes the same bytes (M1-129, register decision 504).
#
# A test reruns when a program it runs changed, by its bytes; a build that
# writes a time or a random identifier into every file it makes would rerun
# every test on every rebuild. Measured on 2026-10-12 by building each tree
# twice from clean and comparing every object, library and program:
#
#   * clang on Windows: 945 of 963 files differed. The compiler stamps each
#     object with the time unless told the linker need not patch it
#     (-mno-incremental-linker-compatible), and lld-link stamps each program
#     unless given /Brepro. With both, 0 of 963.
#   * gcc-14 and clang on Linux: 0 of 642 differed with no flag at all.
#   * MSVC: 947 of 965 differed with /Brepro alone. With /Brepro every object
#     and library is identical once debug information stops going to a shared
#     PDB file (-Zi), but link.exe gives a PDB a new identifier each time it
#     relinks over an existing one and writes it into the program -- 65 bytes
#     of test_half.exe, with every switch Microsoft documents for it tried.
#     The owner's ruling: the MSVC tree builds without debug information, so
#     that it is reproducible like the others. It is the third opinion on the
#     code, run before a milestone, not the tree anyone steps through.
#
# The CTest test `reproducible_build` rebuilds one object and one program in
# place and fails if either comes out different.
# ---------------------------------------------------------------------------
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-mno-incremental-linker-compatible>)
    add_link_options(LINKER:/Brepro)
elseif(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "")
    # CMake's own Debug and RelWithDebInfo link flags are `/debug /INCREMENTAL`.
    # Taken out rather than overridden by a later flag: an incremental link
    # patches the program already on disk instead of writing it afresh, and a
    # relink of reproducible_build_probe.exe with them in place came out
    # different though two builds from clean were identical.
    foreach(kind IN ITEMS EXE SHARED MODULE)
        foreach(config IN ITEMS DEBUG RELWITHDEBINFO)
            separate_arguments(orbsim_link_flags NATIVE_COMMAND "${CMAKE_${kind}_LINKER_FLAGS_${config}}")
            list(REMOVE_ITEM orbsim_link_flags /debug /DEBUG /INCREMENTAL)
            list(JOIN orbsim_link_flags " " CMAKE_${kind}_LINKER_FLAGS_${config})
        endforeach()
    endforeach()
    add_compile_options(/Brepro)
    add_link_options(/Brepro /DEBUG:NONE /INCREMENTAL:NO)
    string(APPEND CMAKE_STATIC_LINKER_FLAGS " /Brepro")
endif()

# ---------------------------------------------------------------------------
# Warnings: every one the compiler has, as errors.
#
# The owner's rule (ADR 0017): as many warnings as possible, as errors, to
# rule out old and bad style mechanically. Carried on an INTERFACE target so
# that every target below gets exactly the same set and none can quietly opt
# out. Third-party headers are included with -isystem (the SYSTEM argument on
# the FetchContent_Declare calls), so only our own code is judged; where our
# code must meet a library's interface in a way a warning objects to -- a C
# varargs API, a pointer and a count, Vulkan's generic function pointer -- the
# warning is switched off at that site, with the reason written there, and
# nowhere else. See CODING_GUIDELINES.md section 1.
#
# A new compiler brings new warnings, and they arrive as errors to be triaged,
# exactly as new clang-tidy checks do. That is the design working.
# ---------------------------------------------------------------------------
add_library(orbsim_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # -Weverything, less the few that contradict this project rather than
    # judge it. Measured over our translation units on 2026-09-11, before any
    # was switched off: 2,818 distinct warnings, 2,705 of them from the first
    # group below.
    target_compile_options(orbsim_warnings INTERFACE
            -Weverything
            # Backward compatibility: these flag every feature newer than
            # C++98, 11, 14, 17 or 20, for code that must still build under an
            # older standard. This project is C++23, and the owner's aim is to
            # rule out *old* style -- these would rule out the new.
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
            -Wno-pre-c++14-compat
            -Wno-pre-c++14-compat-pedantic
            -Wno-pre-c++17-compat
            -Wno-pre-c++17-compat-pedantic
            -Wno-pre-c++20-compat
            -Wno-pre-c++20-compat-pedantic
            -Wno-pre-c++23-compat
            -Wno-pre-c++23-compat-pedantic
            # Demands a default: in every switch. Our switches over our own
            # enums are exhaustive with no default, so that -Wswitch reports a
            # newly added enumerator; adding a default would silence that, and
            # -Wcovered-switch-default would then report the default itself.
            # The two cannot both be satisfied. Ruled 2026-09-11.
            -Wno-switch-default
            # Reports struct padding: layout, not style or correctness. The
            # fixes are reordering members away from their logical order or
            # adding dead padding fields. A struct whose layout is an
            # interface -- one the GPU reads -- needs its size and offsets
            # asserted, which this warning would not do either. Ruled
            # 2026-09-11; 10 sites when measured.
            -Wno-padded
            -Werror
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # gcc has no -Weverything. cmake/GccWarnings.cmake is the nearest thing:
    # every warning g++ lists for C++, generated by scripts/gcc-warnings.py,
    # which also holds the few switched off and why. Re-run it on a gcc
    # upgrade.
    orbsim_configure_input("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GccWarnings.cmake")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GccWarnings.cmake)
    target_compile_options(orbsim_warnings INTERFACE ${ORBSIM_GCC_WARNINGS} -Werror)
elseif(MSVC)
    # The third implementation, wired to the `windows-msvc` preset on
    # 2026-09-14. It builds the whole tree, renderer included, which neither
    # Linux preset does -- so it is the only place our Vulkan and SDL code meets
    # a compiler other than clang.
    #
    # /permissive- is the important one: it turns off the Microsoft extensions
    # that would otherwise accept code the other two reject, which is the entire
    # point of having a third front end. /WX matches the -Werror above.
    #
    # **/Wall, not /W4**, since 2026-09-17, which is ADR 0017's rule applied to
    # the third compiler as it is to the other two.
    #
    # The folklore is that /Wall is unusable because it reports through system
    # headers. Measured here rather than repeated: over our own translation
    # units -- dependencies are SYSTEM and unjudged -- /Wall produced warnings
    # in eight codes, and every one of them falls into the four groups below:
    # 73 distinct lines across six codes on the first pass, then two more codes
    # that only appear once the first six stop halting the build. Nothing like the thousands the folklore
    # promises, because the folklore is about projects that compile their
    # dependencies' headers as their own.
    #
    # Two diagnostics are answered at their sites rather than here, because they
    # fire on our own clang-specific annotations and not on a library's
    # interface: C4068 (unknown pragma 'clang') is guarded with #ifdef __clang__
    # around each region, and C5030 (unrecognised attribute) by the
    # ORBSIM_LIFETIMEBOUND macro in render/VulkanHandle.hpp.
    target_compile_options(orbsim_warnings INTERFACE
            /Wall
            # 52 lines. "Unreferenced inline function has been removed": fires
            # for every inline a translation unit includes and does not call,
            # which in a header-heavy codebase is most of them. It reports the
            # linker doing its job, not a property of the code.
            /wd4514
            # 12 lines. Struct padding -- the same diagnostic as clang's
            # -Wpadded, which this project switched off on 2026-09-11 with its
            # reason written above. Consistency, not a second decision.
            /wd4820
            # 9 lines between them. "Copy constructor / assignment operator /
            # move constructor / move assignment / default constructor was
            # implicitly defined as deleted." That is Rule of Zero working: a
            # class holding a move-only handle has no copy, and CODING_GUIDELINES
            # section 4 is why it declares nothing. Writing `= delete` at each
            # site would silence these and is the alternative -- 9 sites, and it
            # trades the rule for the boilerplate the rule exists to avoid.
            /wd4623 /wd4625 /wd4626 /wd5026 /wd5027
            # 66 lines. "Compiler may not enforce left-to-right evaluation order
            # in a brace enclosed initializer list." Since C++17 the standard
            # *does* guarantee that order, and /permissive- above is what asks
            # MSVC to be conformant; the warning is MSVC declining to promise
            # what the language already requires. It fires on designated
            # initializers, which this codebase uses everywhere on purpose.
            /wd4868
            # 32 lines. "Compiler will insert Spectre mitigation for memory load
            # **if /Qspectre switch specified**" -- and it is not specified. A
            # remark about a flag we do not pass, on every indexed load.
            /wd5045
            /WX /permissive-
            # Not a warning switch. MSVC otherwise reads a UTF-8 source file as
            # the machine's ANSI code page, and mp-units' unit symbols are UTF-8
            # -- micro, ohm, euro. Without this the build dies with 47 instances
            # of C3872 ("this character is not allowed in an identifier")
            # pointing into headers that are perfectly well-formed. clang and
            # gcc assume UTF-8 already, so this makes the three agree rather
            # than adding a behaviour.
            /utf-8
    )
endif()

# ---------------------------------------------------------------------------
# Floating point. See CODING_GUIDELINES.md section 11.
#
# FMA contraction reorders arithmetic, and reordered arithmetic breaks the
# round-trip identities the orbital tests depend on. -ffast-math is never set,
# at any optimisation level: orbitInfo() returns infinity on purpose.
#
# PUBLIC on orbsim_core below, so anything computing against the physics
# inherits the same arithmetic. Since 2026-09-26 the same flags are also set
# for every file compiled, beside the instruction set (see above); this target
# stays, because it is where the physics states what arithmetic it relies on.
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
# ORBSIM_SANITIZE_UNDEFINED refuses to configure on Windows, and the reason it
# gives has changed. It used to say UBSan's Windows support is partial. That
# did not survive measurement: over eight kinds of undefined behaviour --
# signed overflow, division by zero, an over-wide shift, an out-of-range float
# cast, a null dereference, a misaligned store, an invalid bool and an invalid
# enum -- Windows and Linux gave the same verdict on every one (2026-09-12),
# which is why the fuzz target below runs UBSan on Windows.
#
# What has *not* been measured is this whole project under it: the renderer,
# its dependencies and the suites, rather than eight cases in a probe. The
# option stays shut until that is measured rather than being opened on the
# strength of a measurement of something else.
# ---------------------------------------------------------------------------
option(ORBSIM_SANITIZE_ADDRESS "Build with AddressSanitizer" OFF)
option(ORBSIM_SANITIZE_UNDEFINED "Build with UndefinedBehaviorSanitizer (not on Windows)" OFF)

# Fuzzing needs the same two Windows arrangements the sanitizers do, and needs
# them before any target is defined, so the option is declared here and the
# targets are built at the bottom of this file. See that section for the rest.
option(ORBSIM_BUILD_FUZZERS "Build the libFuzzer targets" OFF)

# clang's ASan on Windows links against a runtime DLL that lives in the
# compiler's own tree, not on PATH. Without it every instrumented executable
# dies at startup with STATUS_DLL_NOT_FOUND (0xc0000135) before printing
# anything, which is a miserable way to learn this. Both callers below put it
# where their executables are.
function(orbsim_find_asan_runtime out_var)
    execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -print-runtime-dir
            OUTPUT_VARIABLE orbsim_runtime_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE orbsim_runtime_dir_result
    )
    file(GLOB orbsim_asan_dlls "${orbsim_runtime_dir}/clang_rt.asan_dynamic-*.dll")
    if(NOT orbsim_runtime_dir_result EQUAL 0 OR NOT orbsim_asan_dlls)
        message(FATAL_ERROR "no clang_rt.asan_dynamic-*.dll under '${orbsim_runtime_dir}' "
                "(from '${CMAKE_CXX_COMPILER} -print-runtime-dir').")
    endif()
    set(${out_var} ${orbsim_asan_dlls} PARENT_SCOPE)
endfunction()

add_library(orbsim_sanitizers INTERFACE)
if(ORBSIM_SANITIZE_ADDRESS)
    target_compile_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=address -fno-omit-frame-pointer>)
    target_link_options(orbsim_sanitizers INTERFACE
            $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fsanitize=address>)
    if(WIN32)
        # The MSVC STL annotates std::string, std::vector and (since 14.51)
        # std::optional for ASan, and refuses (LNK2038 / lld "failifmismatch:
        # annotate_string") to link an instrumented object against one that is
        # not. vk-bootstrap and Catch2 are built without the sanitizer, so the
        # annotations are switched off here. What is lost is overflow detection
        # *inside* those containers' spare capacity; heap, stack and
        # use-after-free detection are intact.
        #
        # `_DISABLE_OPTIONAL_ANNOTATION` was added on 2026-09-13, when the jump
        # from MSVC 14.44 to 14.51 broke every link in the asan preset with
        # "mismatch detected for 'annotate_optional'". The macro name was read
        # out of the STL's own __msvc_sanitizer_annotate_container.hpp rather
        # than guessed; that header knows four, the fourth being the blanket
        # `_DISABLE_STL_ANNOTATION`. Naming the three is deliberate: it says
        # exactly what is given up, at the price of breaking again the next time
        # the STL annotates a new container -- which is the trade this project
        # usually takes, and it is the reason this comment exists.
        target_compile_definitions(orbsim_sanitizers INTERFACE
                _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION
                _DISABLE_OPTIONAL_ANNOTATION)

        # The build tree is where the executables are, so the DLL goes there.
        orbsim_find_asan_runtime(orbsim_asan_dlls)
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

# What fuzzing needs on Windows, decided here because both settings have to be
# in place before the first target is created. Measured on 2026-09-12, each one
# the answer to a link error rather than a precaution:
#
#   * The *static* C runtime, for every target in the tree. clang ships its
#     Windows libFuzzer built against /MT, and any object that touches the C++
#     standard library stamps its own choice into the object file, so a /MD
#     tree and that library cannot be linked together at all ("failifmismatch:
#     RuntimeLibrary"). The release CRT, not the debug one, even in a Debug
#     build: ASan and /MDd or /MTd do not coexist, for the reason given above.
#   * The MSVC STL's container annotations off, for the same reason the asan
#     preset switches them off: they are written for MSVC's own ASan runtime
#     and will not link against clang's ("failifmismatch: annotate_string").
#     The optional one joined the list on 2026-09-13 with MSVC 14.51 -- see the
#     longer note beside orbsim_sanitizers above.
#
# Neither is needed on Linux, and neither is set there.
if(ORBSIM_BUILD_FUZZERS AND WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_definitions(_DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION
            _DISABLE_OPTIONAL_ANNOTATION)
endif()

# The application needs the Vulkan SDK for the loader and glslc, which a
# headless machine does not have -- the WSL box the Linux presets run on, for
# one. The core and its tests never did (section 12), and this option is what
# makes that claim checkable.
option(ORBSIM_BUILD_APP "Build the Vulkan renderer and application" ON)

# The coverage hook (M1-131, register decisions 559, 566 and 644): with it on,
# every function of this project's own code sets a flag of its own when it is
# entered, and tests/CoverageRecorder.cpp writes down the entered ones when a
# program ends, so that M1-132 can choose tests function by function. Off by
# default; the loops at the end of this file give the targets the hook, the
# linker's map and the recorder, and scripts/check-coverage-hook.py checks
# what they made. Only clang has this hook (decision 625), and the recorder is
# Windows code -- the section markers lld-link does not supply, and Windows'
# own calls -- so clang elsewhere would fail later, with an error naming
# neither (decision 640): both refused here, saying why.
option(ORBSIM_COVERAGE_HOOK "Record the functions each program enters (clang on Windows only)" OFF)
if(ORBSIM_COVERAGE_HOOK)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "ORBSIM_COVERAGE_HOOK needs clang: the hook is clang's "
                "-fsanitize-coverage=func,inline-bool-flag,pc-table, and this tree compiles with "
                "${CMAKE_CXX_COMPILER_ID} (register decision 625).")
    endif()
    if(NOT WIN32)
        message(FATAL_ERROR "ORBSIM_COVERAGE_HOOK needs Windows: the recorder, tests/CoverageRecorder.cpp, "
                "supplies the section markers lld-link does not and calls Windows itself, so it would not "
                "build here (register decision 640).")
    endif()
endif()

# ---------------------------------------------------------------------------
# mp-units -- the dimension system behind core/Units.hpp (ADR 0019).
#
# Header-only, licence MIT, no dependency of its own in this configuration.
# PUBLIC on orbsim_core because core/Units.hpp names mp_units types in its
# interface, so every consumer needs the headers; SYSTEM so -Weverything and
# /Wall judge our code and not theirs.
#
# MP_UNITS_API_CONTRACTS=NONE, deliberately, and **set explicitly rather than
# left to the default**. mp-units decides this with
# `#if MP_UNITS_API_CONTRACTS == 2 || (!defined MP_UNITS_API_CONTRACTS &&
# __has_include(<gsl-lite/gsl-lite.hpp>))`, so a gsl-lite that arrived on the
# include path for any other reason would switch contract checking on by
# itself. Setting the variable is what makes that impossible.
#
# The default is GSL-LITE, which needs gsl-lite to check the library's own
# preconditions. This project already has a contract facility
# (core/Contract.hpp, ADR 0002) and a second would be both a new dependency and
# a second answer to one question. Measured 2026-09-18 before settling it: 54 of
# the library's 60 assertions are compile-time and enforced anyway, five are in
# text formatting this project never does, and the remaining six are all
# division-by-zero guards -- a condition ADR 0002 says to *report*, not assert.
# PROJECT_STATE section 7 question 11 carries the full reckoning and the cheap
# way to reopen it.
#
# MP_UNITS_BUILD_INSTALL=OFF because nothing installs this project, and an
# install rule on a FetchContent dependency leaks into ours.
#
# Pinned at v2.5.0 (24 December 2025), the current release. Upstream tests
# clang 16-21; this project runs 23.1.0, which is newer than anything in their
# matrix -- clang 23 was added to it only on master. Measured rather than
# assumed on 2026-09-17: v2.5.0 builds clean here under clang 23.1.0, gcc-14
# and MSVC 14.51, with zero clang-tidy findings escaping its headers.
# ---------------------------------------------------------------------------
set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)
set(MP_UNITS_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mp_units
        GIT_REPOSITORY https://github.com/mpusz/mp-units.git
        GIT_TAG        v2.5.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  src
        SYSTEM
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(mp_units)

# ---------------------------------------------------------------------------
# ERFA -- the astronomy (ADR 0016, M1-05).
#
# The BSD-3-Clause edition of the IAU's SOFA library, copyright the NumFOCUS
# Foundation. It computes TDB - TT now, and the celestial-to-terrestrial
# rotation and the Sun from M1-07 and M1-08, called only from src/astro/
# through typed wrappers. THIRD_PARTY.md has the licence.
#
# Pinned at v2.0.1 (2023-10-13), still the latest release when re-verified on
# 2026-09-19. Master was twelve commits ahead that day and unreleased; the one
# that touches code, "Make leap seconds threadsafe" (2026-09-03), is about
# eraDat and what is built on it, which this project does not call -- the leap
# seconds are core/LeapSeconds.hpp's (ADR 0009, register decision 28).
#
# ERFA has no CMake build, so it is built here, **unedited**, as its own static
# C library:
#
#   * every library file upstream's own builds compile -- all of src/*.c but
#     the two validation programs -- and configuration stops unless that is
#     exactly the 249 they list, so that a changed pin fails loudly rather than
#     compiling a different set (register decision 63);
#   * the five version macros its autotools and meson builds generate, read out
#     of upstream's meson.build rather than transcribed, and the version checked
#     against the pin;
#   * with this project's floating-point settings, and the sanitizers when they
#     are on -- but none of its warnings: it is not our code;
#   * its include directory SYSTEM, which keeps -Weverything and clang-tidy off
#     its headers where src/astro/ includes them. That matters more than usual
#     here: the lint header filter matches any path with /src/ in it, and
#     _deps/erfa-src/src/erfa.h has one.
#
# Measured before the pin, on 2026-09-19: all 249 files compile with zero
# warnings under -Wall -Wextra -Wpedantic -Wconversion -Wshadow
# -Wdouble-promotion, and both validation programs pass, under clang 23.1.0,
# clang 23.1.1 with ASan and UBSan, gcc-14 at -O0 and -O2, and MSVC 19.51 at /W3.
#
# PRIVATE on orbsim_core, so nothing that includes an orbsim header can see
# ERFA's: they are included from src/astro/*.cpp and nowhere else, and the
# header self-check below proves it, because its translation units link
# orbsim_core and would not find erfa.h.
# ---------------------------------------------------------------------------
set(ORBSIM_ERFA_VERSION 2.0.1)
FetchContent_Declare(erfa
        GIT_REPOSITORY https://github.com/liberfa/erfa.git
        GIT_TAG        v${ORBSIM_ERFA_VERSION}
        GIT_SHALLOW    TRUE
        SYSTEM
)
# ERFA has no CMakeLists.txt, so this only fetches it; the target is below.
FetchContent_MakeAvailable(erfa)

file(READ "${erfa_SOURCE_DIR}/meson.build" orbsim_erfa_meson)
string(REGEX MATCH "\n    version: '([0-9]+)\\.([0-9]+)\\.([0-9]+)'" orbsim_erfa_match
        "${orbsim_erfa_meson}")
set(orbsim_erfa_upstream_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
if(NOT orbsim_erfa_upstream_version STREQUAL ORBSIM_ERFA_VERSION)
    message(FATAL_ERROR "ERFA: pinned at ${ORBSIM_ERFA_VERSION}, but its meson.build says "
            "'${orbsim_erfa_upstream_version}'. Is FETCHCONTENT_SOURCE_DIR_ERFA pointing at "
            "another checkout? See PROJECT_STATE.md section 8.")
endif()
set(orbsim_erfa_version_definitions
        PACKAGE_VERSION="${ORBSIM_ERFA_VERSION}"
        PACKAGE_VERSION_MAJOR=${CMAKE_MATCH_1}
        PACKAGE_VERSION_MINOR=${CMAKE_MATCH_2}
        PACKAGE_VERSION_MICRO=${CMAKE_MATCH_3}
)
string(REGEX MATCH "set_quoted\\('SOFA_VERSION', '([0-9]+)'\\)" orbsim_erfa_match
        "${orbsim_erfa_meson}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "ERFA: no SOFA_VERSION in its meson.build")
endif()
list(APPEND orbsim_erfa_version_definitions SOFA_VERSION="${CMAKE_MATCH_1}")

file(GLOB orbsim_erfa_sources "${erfa_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM orbsim_erfa_sources
        "${erfa_SOURCE_DIR}/src/t_erfa_c.c"
        "${erfa_SOURCE_DIR}/src/t_erfa_c_extra.c")
list(LENGTH orbsim_erfa_sources orbsim_erfa_count)
if(NOT orbsim_erfa_count EQUAL 249)
    message(FATAL_ERROR "ERFA ${ORBSIM_ERFA_VERSION}: found ${orbsim_erfa_count} library "
            "files where upstream lists 249 (register decision 63)")
endif()

add_library(orbsim_erfa STATIC ${orbsim_erfa_sources})
target_include_directories(orbsim_erfa SYSTEM PUBLIC "${erfa_SOURCE_DIR}/src")
target_compile_definitions(orbsim_erfa PRIVATE ${orbsim_erfa_version_definitions})
target_link_libraries(orbsim_erfa PRIVATE orbsim_fp orbsim_sanitizers)
# sin, cos and the rest live in libm on Linux, and a C program -- the two
# validation programs below -- does not get it through a C++ runtime.
if(NOT WIN32)
    target_link_libraries(orbsim_erfa PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# Simulation core
#
# Deliberately free of any dependency on the renderer or on SDL: the physics
# must run headless, both for tests and for a future scenario batch-runner.
# ---------------------------------------------------------------------------
set(ORBSIM_CORE_SOURCES
        src/astro/EarthOrientation.cpp
        src/astro/Sun.cpp
        src/astro/Tdb.cpp
        src/core/Contract.cpp
        src/core/LeapSeconds.cpp
        src/orbit/Orbit.cpp
)
add_library(orbsim_core STATIC ${ORBSIM_CORE_SOURCES})
target_include_directories(orbsim_core PUBLIC src)
target_link_libraries(orbsim_core
        PUBLIC orbsim_fp mp-units::mp-units
        PRIVATE orbsim_warnings orbsim_sanitizers orbsim_erfa
)
# std::stacktrace, which a failed precondition prints (register decisions 549
# and 556), lives in libstdc++'s separate library, stdc++exp, in gcc 14's
# standard library -- the one both compilers use on Linux. Microsoft's standard
# library carries it itself.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(orbsim_core PUBLIC stdc++exp)
endif()

# ---------------------------------------------------------------------------
# Image files: stb, lodepng and OpenEXR (M1-16; register decisions 195-197).
#
# What a probe writes -- two PNGs and an EXR -- is encoded in orbsim_view, so
# these are declared before it. All three are SYSTEM, so their headers are not
# judged as ours, and none is built with this project's warning set: lodepng's
# source raises 1,841 warnings under it and none under the compiler's defaults
# (measured 2026-09-26), which is the same arrangement ERFA has.
#
# **stb** has no releases or tags, so it is pinned by commit (2026-08-02).
# Only its decoder is used: by the tests, to read both PNGs back independently
# of the encoder that wrote them, and since M1-17 by orbsim_view, to read
# golden images. Header-only: there is nothing to build until a translation
# unit defines STB_IMAGE_IMPLEMENTATION, which src/view/StbImageImpl.cpp does,
# compiled once as orbsim_stb_image for both (register decision 235).
#
# **lodepng** writes both PNGs, 8 and 16 bits per channel. No releases or tags
# either, so pinned by commit (2026-05-28). Its file I/O is compiled out: paths
# are written by the application through std::filesystem::path, which a C
# library's `char*` cannot be trusted with on Windows (CODING_GUIDELINES
# section 18). Its decoder is compiled out too -- this project only encodes
# with it, and a decoder is the part of a PNG library that faces untrusted
# bytes.
#
# **OpenEXR 3.5.1** writes the EXR, through its C library OpenEXRCore only.
# It needs SSE4.1 under this project's clang, which ADR 0023 provides for
# every file. Its own dependencies are the ones it vendors -- libdeflate,
# OpenJPH and zstd -- forced to the vendored copies so that the build does not
# change with whatever the machine has installed, and Imath, which it fetches
# and which is pinned here at v3.2.3 rather than left at its default of
# "main". Static, with its tools, examples, tests, installation and threading
# off: one thread writes one file, and threads would only add a way for the
# output to depend on timing. EXCLUDE_FROM_ALL, so only what orbsim_view links
# is built.
# ---------------------------------------------------------------------------
FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
        SYSTEM
)
FetchContent_Declare(lodepng
        GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
        GIT_TAG        ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a
        SYSTEM
)
# Neither has a CMakeLists.txt, so these only fetch; the targets are below.
FetchContent_MakeAvailable(stb lodepng)

add_library(orbsim_stb INTERFACE)
target_include_directories(orbsim_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
# stb_image's implementation, once: orbsim_view and the tests each compiling it
# would define every function twice. Built with this project's warnings; the
# header is SYSTEM, so what it expands is not judged as ours.
set(ORBSIM_STB_IMAGE_SOURCES src/view/StbImageImpl.cpp)
add_library(orbsim_stb_image STATIC ${ORBSIM_STB_IMAGE_SOURCES})
target_link_libraries(orbsim_stb_image
        PUBLIC orbsim_stb
        PRIVATE orbsim_warnings orbsim_sanitizers
)

add_library(orbsim_lodepng STATIC "${lodepng_SOURCE_DIR}/lodepng.cpp")
target_include_directories(orbsim_lodepng SYSTEM PUBLIC "${lodepng_SOURCE_DIR}")
target_compile_definitions(orbsim_lodepng PUBLIC LODEPNG_NO_COMPILE_DISK LODEPNG_NO_COMPILE_DECODER)

set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_LIBRARIES OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_ENABLE_THREADING OFF CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_DEFLATE ON CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_OPENJPH ON CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_ZSTD ON CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_IMATH ON CACHE BOOL "" FORCE)
set(OPENEXR_IMATH_TAG v3.2.3 CACHE STRING "" FORCE)
set(IMATH_INSTALL OFF CACHE BOOL "" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(openexr
        GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openexr.git
        GIT_TAG        v3.5.1
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
)
# Static: a normal variable, which OpenEXR's option() leaves alone (CMP0077),
# scoped to this call so that SDL's own choice below is untouched.
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(openexr)
unset(BUILD_SHARED_LIBS)

# ---------------------------------------------------------------------------
# orbsim_view -- the render-side maths that Vulkan never touches (ADR 0012).
#
# It links orbsim_core and, privately since M1-16, the image libraries
# above -- nothing graphical. orbsim_render and the application
# link it; orbsim_core does not, and cannot, which is what makes "nothing
# under tests/ includes a Vulkan or SDL header" a property of the link graph
# rather than of anyone's memory.
#
# STATIC since M1-11, which added Camera.cpp. It was INTERFACE while M1-09 and
# M1-10 left it header-only, because a sourceless static library does not
# configure at all -- CMake stops with "No SOURCES given to target" (measured
# 2026-09-20). Nothing else about it changed then: it linked orbsim_core and
# nothing else until M1-16's image libraries, and orbsim_core still cannot
# link it back.
# ---------------------------------------------------------------------------
set(ORBSIM_VIEW_SOURCES
        src/view/BenchmarkPath.cpp
        src/view/Camera.cpp
        src/view/CameraController.cpp
        src/view/CameraPath.cpp
        src/view/FileWrite.cpp
        src/view/FrameStatistics.cpp
        src/view/GoldenPath.cpp
        src/view/GpuClock.cpp
        src/view/ImageCompare.cpp
        src/view/ImageFiles.cpp
        src/view/Lambert.cpp
        src/view/LineBatch.cpp
        src/view/PlanetaryGrid.cpp
        src/view/ProbeImage.cpp
        src/view/ProbeSidecar.cpp
)
add_library(orbsim_view STATIC ${ORBSIM_VIEW_SOURCES})
# Privately, since M1-16: the PNG and EXR encoders' two libraries, and since
# M1-17 the PNG decoder that reads goldens, seen by view/ImageFiles.cpp alone.
# None is a graphics library, so the claim above -- nothing under tests/
# reaches Vulkan or SDL through this target -- stands.
target_link_libraries(orbsim_view
        PUBLIC orbsim_core
        PRIVATE orbsim_warnings orbsim_sanitizers orbsim_lodepng OpenEXR::OpenEXRCore
                orbsim_stb_image
)

# The structural claim, checked rather than remembered (ADR 0012, and
# VERIFICATION.md rule 21: a check not reachable from the build is an
# intention). Costs microseconds, runs at configure time in every tree. The
# other half -- that -DORBSIM_BUILD_APP=OFF still builds the core and its
# tests -- is a second configure and stays a manual step, in M1-09's document.
get_target_property(orbsim_core_links orbsim_core LINK_LIBRARIES)
if(orbsim_core_links MATCHES "orbsim_view")
    message(FATAL_ERROR
            "orbsim_core links orbsim_view. The dependency is one-way: the physics "
            "must not see the renderer's maths (ADR 0012, CODING_GUIDELINES section 12).")
endif()

# ---------------------------------------------------------------------------
# Tests
#
# Catch2 replaced a hand-rolled harness on 2026-09-09. The harness was the
# right call for one file and the wrong one for the suites this milestone adds
# -- counted on 2026-09-13, its task documents name 51 distinct
# tests/test_*.cpp files, so about 48 new ones, where this comment said ten
# (CODING_GUIDELINES.md section 17, and the Toolbox).
#
# Fetched and pinned exactly as the graphics dependencies below are, and
# SYSTEM for the same reason: its headers are not our code, and this project
# compiles with -Wconversion and -Werror. Licence: Boost Software License 1.0.
#
# Declared out here rather than inside ORBSIM_BUILD_APP, because the core
# suites are precisely what must still build when the renderer does not.
# ---------------------------------------------------------------------------
include(CTest)

FetchContent_Declare(catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.0
        GIT_SHALLOW    TRUE
        SYSTEM
)
FetchContent_MakeAvailable(catch2)

# Catch2 needs no ASan container-annotation workaround of its own, which is not
# obvious and was measured rather than assumed: it is built without the
# sanitizer and uses std::string throughout, which is exactly the shape that
# breaks vk-bootstrap above. It links because the _DISABLE_*_ANNOTATION
# definitions orbsim_sanitizers already puts on *our* instrumented translation
# units make the MSVC STL emit detect_mismatch("annotate_string", "0") from our
# side too -- the same value an uninstrumented library emits, so there is no
# mismatch to resolve. Adding the definitions to Catch2 as well changes
# nothing: the asan preset builds and passes 3,632 assertions either way, with
# all 108 of Catch2's objects rebuilt each time to be sure the test was real.

# catch_discover_tests lives in Catch2's extras, not on the default module path.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# The fixtures are under tests/, included from the repository root as
# "tests/OrbitTestSupport.hpp" so a test reads the same way as a source file
# does. Catch2WithMain supplies main(), so no suite declares one.
#
# A library with one source rather than headers alone: the matchers are
# polymorphic, as Catch2 makes them, and OrbitTestSupport.cpp is where their
# vtables are emitted, once (-Wweak-vtables, ADR 0017). PUBLIC, so that every
# suite still gets the warnings and the sanitizers, and so does this source.
set(ORBSIM_TEST_SUPPORT_SOURCES
        tests/FixtureFile.cpp
        tests/HdrDumpFile.cpp
        tests/OrbitTestSupport.cpp
        tests/TestReads.cpp
)
add_library(orbsim_test_support STATIC ${ORBSIM_TEST_SUPPORT_SOURCES})
target_include_directories(orbsim_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orbsim_test_support PUBLIC
        orbsim_core
        orbsim_warnings
        orbsim_sanitizers
        orbsim_stb_image
        Catch2::Catch2WithMain
)
# Where probes write by default -- the build tree, register decision 192 --
# named here rather than with the application, because the suites read from
# it too.
set(ORBSIM_PROBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/probes")
# Where the suites find what they read, and where each test case records what
# it read (M1-129, register decisions 498 and 527-530): data/ in the source
# tree, named exactly as ORBSIM_ASSET_DIR names it for the application; the
# probes' folder; and test-reads/ in the build tree, which scripts/run-tests.py
# reads. Private, because only tests/TestReads.cpp reads them -- dataFile() and
# probeFile() are the one place a folder a test reads from enters C++, and the
# CTest test test_reads fails if another file under tests/ names one.
target_compile_definitions(orbsim_test_support PRIVATE
        ORBSIM_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        ORBSIM_PROBE_DIR="${ORBSIM_PROBE_DIR}"
        ORBSIM_READ_RECORD_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-reads"
)
# Optimised in every clang and gcc tree (register decision 538): each test case
# hashes the files it read as it ends, and unoptimised SHA-256 runs at about
# 10 MB/s -- a port check took 7.9 s in the Debug tree against 2.4 s without
# hashing, and 2.8-2.9 s with this. The standard library's Debug checks stay
# on. Not for MSVC, whose Debug run-time checks cl refuses beside /O2 (D8016).
set_source_files_properties(tests/TestReads.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,GNU>:-O2>")

set(ORBSIM_TESTS
        test_astro_time
        test_benchmark_path
        test_camera
        test_camera_controller
        test_camera_path
        test_double_double
        test_earth_orientation
        test_exposure
        test_file_replace
        test_fixture_file
        test_golden_path
        test_gpu_clock
        test_half
        test_hdr_dump
        test_image_compare
        test_image_files
        test_lambert
        test_line_batch
        test_math
        test_orbit
        test_orbit_elements
        test_orbit_scales
        test_pipeline_inputs
        test_planetary_grid
        test_probe_sidecar
        test_projection
        test_read_records
        test_render_quality
        test_srgb
        test_statistics
        test_sun
        test_test_case
        test_time
        test_time_leap
        test_time_ut1
        test_tonemap
        test_units_validated
        test_view_math
        test_writer_turns
)
# The suites, each with the abort listener (M1-90): cmake/TestSuites.cmake.
orbsim_configure_input("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestSuites.cmake")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestSuites.cmake)

# What test_read_records holds a recorded file's bytes to (register decision
# 554): the SHA-256 CMake computes of the same file, a second implementation
# beside tests/Sha256.hpp and every byte rather than the size --
# std::filesystem::file_size, which would give the size, is where
# clang-analyzer reports a false finding inside MSVC's own header. A configure
# dependency, so that an edit of the file configures the tree again.
set(orbsim_read_back_file "${CMAKE_CURRENT_SOURCE_DIR}/data/skyfield/README.md")
file(SHA256 "${orbsim_read_back_file}" orbsim_read_back_sha256)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${orbsim_read_back_file}")
target_compile_definitions(test_read_records PRIVATE
        ORBSIM_READ_BACK_SHA256="${orbsim_read_back_sha256}")

# The suites that link orbsim_view. Named here rather than added to
# orbsim_test_support, so that the other suites still cannot see the
# render-side maths and the link graph keeps saying so.
target_link_libraries(test_view_math PRIVATE orbsim_view)
target_link_libraries(test_projection PRIVATE orbsim_view)
target_link_libraries(test_camera PRIVATE orbsim_view)
target_link_libraries(test_camera_controller PRIVATE orbsim_view)
target_link_libraries(test_camera_path PRIVATE orbsim_view)
target_link_libraries(test_render_quality PRIVATE orbsim_view)
target_link_libraries(test_pipeline_inputs PRIVATE orbsim_view)
target_link_libraries(test_srgb PRIVATE orbsim_view)
target_link_libraries(test_exposure PRIVATE orbsim_view)
target_link_libraries(test_tonemap PRIVATE orbsim_view)
target_link_libraries(test_half PRIVATE orbsim_view)
target_link_libraries(test_hdr_dump PRIVATE orbsim_view)
target_link_libraries(test_probe_sidecar PRIVATE orbsim_view)
target_link_libraries(test_image_compare PRIVATE orbsim_view)
target_link_libraries(test_golden_path PRIVATE orbsim_view)
target_link_libraries(test_lambert PRIVATE orbsim_view)
target_link_libraries(test_line_batch PRIVATE orbsim_view)
target_link_libraries(test_planetary_grid PRIVATE orbsim_view)
target_link_libraries(test_statistics PRIVATE orbsim_view)
target_link_libraries(test_gpu_clock PRIVATE orbsim_view)
target_link_libraries(test_benchmark_path PRIVATE orbsim_view)
target_link_libraries(test_file_replace PRIVATE orbsim_view)
# And OpenEXR itself: the EXR is read back through the reference
# implementation's own decoder, which is the check that the file is one.
target_link_libraries(test_image_files PRIVATE orbsim_view OpenEXR::OpenEXRCore)

# The run-time half of Count's wraparound guard (M1-12, register decision 136).
#
# Programs of their own, because the claim is that an assertion fires and an
# assertion that fires calls abort -- which Catch2 cannot survive, taking the
# reporter and every case that had not run yet with it. The script decides which
# way round the exit code means, and reports SKIPPED rather than passing in a
# tree where assertions are not live. See cmake/VerifyAssertionProbe.cmake.
#
# **One per operation**, because a program can only abort once. There was a
# single probe for one day and it covered the subtraction only; deleting the
# run-time guard from `operator+` then passed the whole of `check` -- 200 tests,
# exit 0. Decision 125 guards three operations, so three probes.
set(ORBSIM_COUNT_WRAPAROUND_OPERATIONS subtraction addition multiplication)
foreach(operation IN LISTS ORBSIM_COUNT_WRAPAROUND_OPERATIONS)
    add_executable(count_wraparound_${operation} tests/count_wraparound_${operation}.cpp)
    # The repository root, for `#include "tests/CountWraparound.hpp"`. These
    # deliberately do not link orbsim_test_support -- they need none of it, and
    # not linking it keeps Catch2 out of a program whose whole job is to abort.
    target_include_directories(count_wraparound_${operation} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(count_wraparound_${operation} PRIVATE
            orbsim_core
            orbsim_warnings
            orbsim_sanitizers
    )
    add_test(NAME count_wraparound_${operation}_aborts
            COMMAND ${CMAKE_COMMAND}
                    -DPROBE=$<TARGET_FILE:count_wraparound_${operation}>
                    "-DCLAIM=the count's wraparound guard on ${operation}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAssertionProbe.cmake
    )
    set_tests_properties(count_wraparound_${operation}_aborts PROPERTIES
            SKIP_REGULAR_EXPRESSION "SKIPPED:"
    )
endforeach()

# angleBetween's precondition, that neither vector is zero (M1-118, finding 1.7,
# register decisions 472 and 476): a program that must abort, judged by the same
# script, for the reason the comment above gives.
add_executable(angle_between_zero_probe tests/angle_between_zero_probe.cpp)
# The repository root, for `#include "tests/AbortProbe.hpp"`.
target_include_directories(angle_between_zero_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(angle_between_zero_probe PRIVATE
        orbsim_core
        orbsim_warnings
        orbsim_sanitizers
)
add_test(NAME angle_between_zero_aborts
        COMMAND ${CMAKE_COMMAND}
                -DPROBE=$<TARGET_FILE:angle_between_zero_probe>
                "-DCLAIM=angleBetween's refusal of a zero vector"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAssertionProbe.cmake
)
set_tests_properties(angle_between_zero_aborts PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED:"
)

# What a failed precondition or postcondition says (M1-129, register decisions
# 549, 556 and 557): the condition, the function and the file, and the line
# from the chain of calls wherever the tree has debug information to read it
# from -- every tree but MSVC's, which links without it (decision 511).
# cmake/VerifyAssertionMessage.cmake finds the line in the probe's own source.
add_executable(assertion_message_probe tests/assertion_message_probe.cpp)
target_include_directories(assertion_message_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(assertion_message_probe PRIVATE
        orbsim_core
        orbsim_warnings
        orbsim_sanitizers
)
if(MSVC)
    set(orbsim_message_lines OFF)
else()
    set(orbsim_message_lines ON)
endif()
foreach(kind precondition postcondition)
    if(kind STREQUAL "precondition")
        set(orbsim_tripped provokedPrecondition)
    else()
        set(orbsim_tripped provokedPostcondition)
    endif()
    add_test(NAME assertion_message_${kind}
            COMMAND ${CMAKE_COMMAND}
                    -DPROBE=$<TARGET_FILE:assertion_message_probe>
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/assertion_message_probe.cpp
                    -DKIND=${kind}
                    -DFUNCTION=${orbsim_tripped}
                    -DLINES=${orbsim_message_lines}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAssertionMessage.cmake
    )
    set_tests_properties(assertion_message_${kind} PROPERTIES
            SKIP_REGULAR_EXPRESSION "SKIPPED:"
    )
endforeach()

# The files compiled here and not linted on purpose (M1-135, register decision
# 665): each entry `<path>: <reason>`, added beside the target that compiles
# the file and under the same condition. The CTest test lint_deps fails on a
# compiled file of the repository that is neither linted nor named here, on an
# entry without its reason, and on one naming a file this tree does not
# compile or does lint. Started empty, so that a value given on the command
# line cannot add to it.
set(ORBSIM_LINT_EXCEPTIONS "")

# -Wfloat-equal is live after core/Units.hpp (M1-117, register decisions 451
# and 454): mp-units v2.5.0 left it switched off there, and core/Units.hpp now
# undoes that. tests/float_equal_guard.cpp must NOT compile, so it is a target
# nothing builds -- here only so that the compile database carries its exact
# command -- and it is not linted. The script compiles it from that command,
# rather than building the target, because `check` runs CTest from inside
# Ninja. clang and gcc only: Microsoft's compiler has no -Wfloat-equal, and
# mp-units' slip changes nothing there.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_library(float_equal_guard OBJECT EXCLUDE_FROM_ALL tests/float_equal_guard.cpp)
    list(APPEND ORBSIM_LINT_EXCEPTIONS
            "tests/float_equal_guard.cpp: must not compile -- check-float-equal-guard.py requires the error")
    target_include_directories(float_equal_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(float_equal_guard PRIVATE orbsim_core orbsim_warnings)
    if(ORBSIM_PYTHON)
        add_test(NAME float_equal_guard
                COMMAND ${ORBSIM_PYTHON}
                        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-float-equal-guard.py
                        ${CMAKE_BINARY_DIR}
        )
        add_test(NAME float_equal_guard_self_test
                COMMAND ${ORBSIM_PYTHON}
                        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-float-equal-guard.py --self-test
        )
    else()
        add_test(NAME float_equal_guard COMMAND ${CMAKE_COMMAND} -E false)
        add_test(NAME float_equal_guard_self_test COMMAND ${CMAKE_COMMAND} -E false)
    endif()
endif()

# The same source compiles and links to the same bytes (M1-129, register
# decisions 488 and 504), so that a rebuilt program whose input did not change
# does not look changed to the tests that read it. A program no test runs,
# because the script writes it again while other tests run; it holds the lock
# the mutants_due tests hold, because they read the tree's objects and
# programs. scripts/check-reproducible-build.py says how and why.
add_executable(reproducible_build_probe tests/reproducible_build_probe.cpp)
target_link_libraries(reproducible_build_probe PRIVATE
        orbsim_core
        orbsim_warnings
        orbsim_sanitizers
)
if(ORBSIM_PYTHON)
    add_test(NAME reproducible_build
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-reproducible-build.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_MAKE_PROGRAM}
                    $<TARGET_OBJECTS:reproducible_build_probe>
                    $<TARGET_FILE:reproducible_build_probe>
    )
    add_test(NAME reproducible_build_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-reproducible-build.py --self-test
    )
else()
    add_test(NAME reproducible_build COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME reproducible_build_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(reproducible_build PROPERTIES RESOURCE_LOCK ninja_deps)

# The coverage hook on exactly the project's own code, every program with its
# map, and every test this file defines carrying its own name (M1-131, register
# decisions 584, 632 and 639): scripts/check-coverage-hook.py says what it
# reads and why. In every tree, the hook on or off -- with it off, nothing may
# carry it. It reads the tree's objects and programs, so it holds the lock
# reproducible_build holds while it writes one of each again; and its
# self-test, on made-up objects, programs and test listings.
if(ORBSIM_COVERAGE_HOOK)
    set(orbsim_coverage_hook_state on)
else()
    set(orbsim_coverage_hook_state off)
endif()
if(ORBSIM_PYTHON)
    add_test(NAME coverage_hook
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-coverage-hook.py
                    ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CTEST_COMMAND}
                    --hook ${orbsim_coverage_hook_state}
    )
    add_test(NAME coverage_hook_self_test
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-coverage-hook.py --self-test
    )
else()
    add_test(NAME coverage_hook COMMAND ${CMAKE_COMMAND} -E false)
    add_test(NAME coverage_hook_self_test COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(coverage_hook PROPERTIES LABELS fixtures RESOURCE_LOCK ninja_deps)
set_tests_properties(coverage_hook_self_test PROPERTIES LABELS fixtures)

# The recorder and its self-test (M1-131, register decisions 566, 594, 632,
# 636 and 639), where the hook is on.
#
# The recorder, tests/CoverageRecorder.cpp, writes down the functions a
# program entered, however it ends; the file says how. An OBJECT library, so
# that its object is linked into each program directly and cannot be left out
# as a static library's would be, as orbsim_abort_behaviour is into the suites
# -- the loops at the end of this file link it into every program carrying the
# hook -- and compiled without the hook, which calls it.
#
# The self-test runs three programs built from tests/coverage_recorder_probe.cpp:
# with the hook and the recorder; the same with
# tests/coverage_recorder_unaligned.cpp, whose flags have no address (the hook
# without `pc-table` for that file alone, the flag after the hook's own on the
# command line), so that the recorder must refuse to name any function; and
# coverage_recorder_probe_plain, which compiles the probe and
# src/core/Contract.cpp once more with neither, for the exit code and the
# output each way of stopping has without the recorder's handlers.
# scripts/check-coverage-recorder.py says what it requires of each.
if(ORBSIM_COVERAGE_HOOK)
    add_library(orbsim_coverage_recorder OBJECT tests/CoverageRecorder.cpp)
    # The repository root, for `#include "tests/WriterTurns.hpp"` (decision 648).
    target_include_directories(orbsim_coverage_recorder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(orbsim_coverage_recorder PRIVATE orbsim_warnings orbsim_sanitizers)

    add_executable(coverage_recorder_probe tests/coverage_recorder_probe.cpp)
    add_executable(coverage_recorder_unaligned tests/coverage_recorder_probe.cpp
            tests/coverage_recorder_unaligned.cpp)
    foreach(orbsim_probe IN ITEMS coverage_recorder_probe coverage_recorder_unaligned)
        target_include_directories(${orbsim_probe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(${orbsim_probe} PRIVATE orbsim_core orbsim_warnings orbsim_sanitizers)
    endforeach()
    set_source_files_properties(tests/coverage_recorder_unaligned.cpp PROPERTIES
            COMPILE_OPTIONS -fno-sanitize-coverage=pc-table)

    add_executable(coverage_recorder_probe_plain tests/coverage_recorder_probe.cpp src/core/Contract.cpp)
    target_include_directories(coverage_recorder_probe_plain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} src)
    target_link_libraries(coverage_recorder_probe_plain PRIVATE orbsim_fp orbsim_warnings orbsim_sanitizers)

    if(ORBSIM_PYTHON)
        add_test(NAME coverage_recorder
                COMMAND ${ORBSIM_PYTHON}
                        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-coverage-recorder.py
                        --probe $<TARGET_FILE:coverage_recorder_probe>
                        --unaligned $<TARGET_FILE:coverage_recorder_unaligned>
                        --plain $<TARGET_FILE:coverage_recorder_probe_plain>
                        --config $<CONFIG>
        )
    else()
        add_test(NAME coverage_recorder COMMAND ${CMAKE_COMMAND} -E false)
    endif()
    set_tests_properties(coverage_recorder PROPERTIES LABELS fixtures)
endif()

# The generated reference data is the data the error budgets were measured
# against, or `check` says otherwise (M1-06, register decision 44). Horizons
# output is not redistributed, so a fresh clone has none: the script then
# prints SKIPPED, which CTest reports as a skip in its summary rather than
# counting as a pass. A fixture present with the wrong hash fails.
add_test(NAME horizons_fixture_checksums
        COMMAND ${CMAKE_COMMAND}
                -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/data/horizons
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyFixtureChecksums.cmake
)
set_tests_properties(horizons_fixture_checksums PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED:"
        LABELS fixtures
)

# ERFA's own validation, both programs upstream's `make check` runs (register
# decision 62): t_erfa_c over the library's routines, 1,494 checks, and
# t_erfa_c_extra over the version functions and the leap-second table's getter
# and setter -- erfaversion.c and erfadatextra.c are among the 249 files
# compiled, and only the second program exercises them. They prove the library
# was built right by *this* toolchain with *these* flags, which a pin cannot:
# each exits non-zero on any failure.
foreach(erfa_validation IN ITEMS t_erfa_c t_erfa_c_extra)
    add_executable(${erfa_validation} "${erfa_SOURCE_DIR}/src/${erfa_validation}.c")
    target_link_libraries(${erfa_validation} PRIVATE orbsim_erfa orbsim_fp orbsim_sanitizers)
    add_test(NAME ${erfa_validation} COMMAND ${erfa_validation})
endforeach()

# ---------------------------------------------------------------------------
# Fuzzing. See VERIFICATION.md rule 13 and tests/fuzz_orbit.cpp.
#
# Off by default and not a CTest test: a fuzzer has no natural exit, so it is
# run deliberately with a time or run budget rather than as part of `check`.
#
# Windows is where this runs now (2026-09-12), through the `windows-fuzz`
# preset. It used to refuse to configure here at all, on the claim that clang's
# libFuzzer does not target the MSVC ABI -- which is simply false: it builds,
# it runs, and both sanitizers are live. What it does take is the two
# arrangements made near the sanitizer options above, and the ASan DLL beside
# the executable, below.
#
# `linux-fuzz` stays, for one reason: LeakSanitizer does not exist on Windows.
# Measured the same day, a planted 64-byte leak is reported under WSL and
# passes silently here. Nothing in the fuzzed path allocates today, so it finds
# nothing -- but it is the only place a leak could be found at all, and
# `docs/STATUS.md` says so.
# ---------------------------------------------------------------------------

# The fuzz targets' sources are compiled in every tree as well, into an object
# library nothing links, without libFuzzer: so that the full warning set and
# check's lint see them in trees that do not build the fuzzer itself.
# Until 2026-09-11 nothing linted tests/fuzz_orbit.cpp at all (ADR 0017).
#
# The claim above was half true until 2026-09-13. `lint` did see the source,
# because clang-tidy is driven from the file list rather than from the build
# graph -- but this object library was in `all` and not in `check`, so a warning
# in tests/fuzz_orbit.cpp failed a bare `ninja` and passed the target that
# defines done. Measured with `ninja -t inputs check`, which listed the source
# as a lint input and the object as nothing at all. It is a dependency of
# `check` now, at the bottom of this file.
set(ORBSIM_FUZZ_SOURCES
        tests/fuzz_orbit.cpp
        tests/fuzz_time.cpp
)
add_library(orbsim_fuzz_objects OBJECT ${ORBSIM_FUZZ_SOURCES})
target_link_libraries(orbsim_fuzz_objects PRIVATE orbsim_core orbsim_warnings)

if(ORBSIM_BUILD_FUZZERS)
    # Two targets, one recipe. fuzz_time joined on 2026-09-18 with M1-04: the
    # calendar and the Julian date are the second surface in this project that
    # turns bytes from outside into numbers, which is what rule 13 says to
    # point a fuzzer at.
    foreach(fuzz_target IN ITEMS fuzz_orbit fuzz_time)
        add_executable(${fuzz_target} tests/${fuzz_target}.cpp)
        target_link_libraries(${fuzz_target} PRIVATE orbsim_core orbsim_warnings)
        # The sanitizers come with the fuzzer here rather than through
        # orbsim_sanitizers, so that a fuzz build is always instrumented: an
        # uninstrumented fuzzer finds only the crashes that would have crashed
        # anyway, which is the smallest and least interesting class.
        #
        # The same flags on both platforms. UndefinedBehaviorSanitizer is in
        # that list on Windows too, and it is not doing less there: measured on
        # 2026-09-12 over eight kinds of undefined behaviour -- signed overflow,
        # division by zero, an over-wide shift, an out-of-range float cast, a
        # null dereference, a misaligned store, an invalid bool and an invalid
        # enum -- Windows and Linux gave the same verdict on every one.
        target_compile_options(${fuzz_target} PRIVATE
                -fsanitize=fuzzer,address,undefined
                -fno-sanitize-recover=all
                -fno-omit-frame-pointer)
        target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer,address,undefined)
        if(WIN32)
            # Beside the executable rather than only in the build root, so the
            # fuzzer runs from wherever it is invoked with no PATH set up:
            # without this DLL it does not reach main at all.
            orbsim_find_asan_runtime(orbsim_asan_dlls)
            add_custom_command(TARGET ${fuzz_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${orbsim_asan_dlls} $<TARGET_FILE_DIR:${fuzz_target}>
                    COMMENT "clang_rt.asan_dynamic beside ${fuzz_target}")
        endif()
    endforeach()
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

# The Khronos utility headers. Pinned at the tag matching Vulkan-Headers above,
# which is what makes them safe to use together: a v1.4.x utility header can name
# enumerators a v1.3.302 header does not have.
#
# Licence Apache-2.0 -- the first in this project, and THIRD_PARTY.md records
# which parts are compiled, because the answer is "none of them". EXCLUDE_FROM_ALL
# keeps its two static libraries out of `all`; we link only the INTERFACE target
# Vulkan::UtilityHeaders, which is headers and nothing else.
#
# It replaced a hand-written VkResult-to-string switch that named nineteen
# results and printed a number for the rest. string_VkResult() names all of them
# and is maintained by the people who add them.
FetchContent_Declare(vulkan_utility_libraries
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git
        GIT_TAG        v1.3.302
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
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
FetchContent_MakeAvailable(vulkan_utility_libraries)
FetchContent_MakeAvailable(vk_bootstrap vma sdl3)

# The SDK supplies only the loader import library and the shader compiler.
find_package(Vulkan REQUIRED COMPONENTS glslc)

# VMA ships as a single header; this target holds the one translation unit that
# instantiates its implementation.
add_library(vma_impl STATIC src/render/VmaImpl.cpp)
list(APPEND ORBSIM_LINT_EXCEPTIONS "src/render/VmaImpl.cpp: VMA's implementation, from its one third-party include")
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
        shaders/fullscreen.vert
        shaders/tonemap.frag
        shaders/probe_gradient.frag
        shaders/lambert.vert
        shaders/lambert.frag
        shaders/probe_port.frag
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
# What the renderer and the app see: the core, the render-side maths, plus the
# graphics libraries. One target, so that the header self-check below compiles
# those headers with exactly the view of the world the application has.
#
# **orbsim_view joined on 2026-09-23 with M1-12**, and it is the first time a
# shipping target links it -- until then only three test suites did, which is
# why view/Camera.hpp and view/Projection.hpp existed for three days without
# the application being able to see them. The direction is unchanged and still
# one-way: the application and the renderer link orbsim_view, orbsim_view
# links orbsim_core, and orbsim_core links neither (ADR 0012, and the
# configure-time check beside that target).
add_library(orbsim_render_deps INTERFACE)
target_link_libraries(orbsim_render_deps INTERFACE
        orbsim_core
        orbsim_view
        vma_impl
        vk-bootstrap::vk-bootstrap
        Vulkan::Headers
        Vulkan::UtilityHeaders
        SDL3::SDL3
)

set(ORBSIM_APP_SOURCES
        src/app/BenchMode.cpp
        src/app/InteractiveView.cpp
        src/app/main.cpp
        src/app/ProbeMode.cpp
        src/app/ShaderRecord.cpp
        src/render/LineRenderer.cpp
        src/render/Pipeline.cpp
        src/render/Probes.cpp
        src/render/ResolvePass.cpp
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

# Where probes write by default -- ORBSIM_PROBE_DIR, set beside the test
# support library -- where a benchmark writes by default, the same way (M1-22,
# decision 373), and which configuration built the binary, for the probe's
# sidecar and the benchmark's report.
set(ORBSIM_BENCH_DIR "${CMAKE_CURRENT_BINARY_DIR}/bench")
target_compile_definitions(orbsim PRIVATE
        ORBSIM_SHADER_DIR="${SHADER_OUT_DIR}"
        ORBSIM_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
        ORBSIM_PROBE_DIR="${ORBSIM_PROBE_DIR}"
        ORBSIM_BENCH_DIR="${ORBSIM_BENCH_DIR}"
        ORBSIM_BUILD_CONFIG="$<CONFIG>"
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
# the renderer's regression test. Labelled so that a machine without a GPU can
# leave it out with --label-exclude gpu; `ctest -LE gpu` is the spelling, and
# docs/STATUS.md says what each list comes to.
add_test(NAME orbsim_smoke COMMAND orbsim --validate --seconds 2)
# Every test that needs the GPU holds one lock, so that `check`'s parallel
# ctest never runs two of them at once (M1-91, ADR 0024). The CTest test
# parallel_tests refuses a gpu-labelled test without it.
set_tests_properties(orbsim_smoke PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# The same, with no scene drawn (the phase A gate, register decision 409).
# Since M1-21 the window draws its lines before the resolve pass, and the
# validation layer then counts the lines' push constants as set for the
# resolve pipeline as well, although the two layouts differ -- so a resolve
# pass that never pushes its exposure went unreported in orbsim_smoke, shown
# by bisecting to M1-21 and by skipping the lines. With nothing drawn first,
# the layer sees the resolve pass alone.
add_test(NAME orbsim_smoke_bare COMMAND orbsim --validate --seconds 2 --scene none)
set_tests_properties(orbsim_smoke_bare PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# The probes (M1-16; ADR 0008; register decisions 192-194).
#
# probe_clear renders `clear` under the validation layers into the build
# tree's probes/, where the owner looks at clear.png, and requires exit code 0
# and all five files present and non-empty -- the files checked whatever the
# exit code, so a failing run is seen to have written them too (decision 193).
# probe_clear_determinism renders it again into probes-rerun/ and requires the
# two HDR dumps byte-identical (VERIFICATION.md rule 16). Both are the fixture
# test_probe_clear reads the frame from.
#
# **The golden is a separate test since M1-110** (register decision 293).
# probe_clear_golden renders `clear` again, into probes-golden/, and compares
# it with this graphics card's own golden, tests/golden/<vendor>-<device>/
# clear.png (decisions 287 and 289), requiring it to match (ADR 0008;
# decisions 228-231) -- with a diff image from an "earlier run" planted first,
# which the application must remove (decision 231). Until M1-110 probe_clear
# did both; but a test that sets up a fixture and fails keeps every test that
# needs the fixture from running -- CTest reports them "Not Run" -- so on a
# card with no golden yet the frame checks and the altered-golden tests below
# would not have run at all. Measured with a throwaway CTest project on
# 2026-10-03, CMake 4.3.1. The cost is one more render, about a second.
set(ORBSIM_GOLDEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
add_test(NAME probe_clear
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DPROBE=clear
                -DOUT=${ORBSIM_PROBE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
add_test(NAME probe_clear_golden
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DPROBE=clear
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/probes-golden
                -DGOLDEN_DIR=${ORBSIM_GOLDEN_DIR}
                -DPLANT_DIFF=ON
                "-DEXPECT_STDOUT=: matches"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
# Both clear reruns set up probe_clear_reruns (M1-109): mutants_due_probe_shaders
# reads every sidecar of m1-16's judges, theirs among them, and must read ones
# this build wrote -- in a whole `check` and when CTest is asked for it alone.
set_tests_properties(probe_clear_golden PROPERTIES
        LABELS gpu RESOURCE_LOCK gpu FIXTURES_SETUP probe_clear_reruns)
add_test(NAME probe_clear_determinism
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DPROBE=clear
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/probes-rerun
                -DCOMPARE_WITH=${ORBSIM_PROBE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
set_tests_properties(probe_clear PROPERTIES
        LABELS gpu RESOURCE_LOCK gpu FIXTURES_SETUP "probe_clear_frame;probe_data")
set_tests_properties(probe_clear_determinism PROPERTIES
        LABELS gpu RESOURCE_LOCK gpu FIXTURES_REQUIRED probe_clear_frame
        FIXTURES_SETUP probe_clear_reruns)

# The frame read back and checked number by number (decision 194): an
# ordinary suite of the tests' own, linking no Vulkan, that runs after
# probe_clear has written the dump. The pattern M1-18's numeric probes copy.
add_executable(test_probe_clear tests/test_probe_clear.cpp)
target_link_libraries(test_probe_clear PRIVATE orbsim_abort_behaviour orbsim_test_support)
catch_discover_tests(test_probe_clear
        PROPERTIES FIXTURES_REQUIRED probe_clear_frame LABELS gpu RESOURCE_LOCK gpu)

# The numeric probes (M1-18; ADR 0008, ADR 0014; register decisions 256-271).
#
# **The pattern every later numeric probe copies.** Each probe is a CTest test
# that renders it under the validation layers into the build tree's probes/
# and requires exit code 0 and its five files -- probe_clear's shape, without
# a golden, because a radiometry probe is checked by number and an image would
# be the weaker check (ADR 0008). Each sets up the fixture probe_data; the
# suites that read the frames require it, so CTest renders the probes first,
# and again when only a suite is asked for. The GPU work stays in the
# application; the arithmetic and the reference stay in suites that link no
# Vulkan.
#
# test_radiometry holds the lambert frames to the analytic radiance, 0.5 %
# (decision 259); test_tonemap_port holds every probe's pictures to the CPU
# chain applied to its HDR frame (decisions 260 and 261), clear's included --
# so probe_clear sets up probe_data as well as its own fixture.
#
# **One fixture name per suite, on purpose.** catch_discover_tests passes
# PROPERTIES on as a flat list, so a value holding two names --
# "probe_data;probe_clear_frame" -- is split, its second half read as a
# property name, and every property after it lost: the suite ran without its
# label, its GPU lock or the second fixture, before probe_clear, against
# whatever frame was left in probes/. Found on 2026-10-03, when the port
# check on clear failed in one tree and passed in the other.
set(ORBSIM_NUMERIC_PROBES
        lambert
        lambert-half-au
        lambert-two-au
        lambert-tilted-60
        lambert-exposure
        lambert-backlit
        tonemap-port
        lines
        grid-400km
        grid-jitter-0
        grid-jitter-1
        grid-jitter-2
        grid-jitter-3
        grid-jitter-4
        grid-jitter-1km-0
        grid-jitter-1km-1
        grid-jitter-1km-2
        grid-jitter-1km-3
        grid-jitter-1km-4
)
foreach(probe IN LISTS ORBSIM_NUMERIC_PROBES)
    add_test(NAME probe_${probe}
            COMMAND ${CMAKE_COMMAND}
                    -DORBSIM=$<TARGET_FILE:orbsim>
                    -DPROBE=${probe}
                    -DOUT=${ORBSIM_PROBE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
    set_tests_properties(probe_${probe} PROPERTIES
            LABELS gpu RESOURCE_LOCK gpu FIXTURES_SETUP probe_data)
endforeach()

foreach(suite IN ITEMS test_radiometry test_tonemap_port)
    add_executable(${suite} tests/${suite}.cpp)
    target_link_libraries(${suite} PRIVATE orbsim_abort_behaviour orbsim_test_support orbsim_view)
endforeach()
catch_discover_tests(test_radiometry
        PROPERTIES FIXTURES_REQUIRED probe_data LABELS gpu RESOURCE_LOCK gpu)
catch_discover_tests(test_tonemap_port
        PROPERTIES FIXTURES_REQUIRED probe_data LABELS gpu RESOURCE_LOCK gpu)

# The `lines` probe (M1-19; register decisions 280, 281 and 283). It is in the
# list above, so probe_lines renders it without a golden and sets up
# probe_data; test_probe_lines then holds every axis tip and square corner to
# the pixel the pinhole camera puts it on, in its colour -- a check that is
# the same on every graphics card (decision 281). And, unlike the radiometry
# probes, it has a golden too, compared by probe_lines_golden on a render of
# its own, as probe_clear_golden compares `clear` (decision 293): one golden
# per card (decision 287), so a card with none fails this test by name and
# leaves the number check running.
add_executable(test_probe_lines tests/test_probe_lines.cpp)
target_link_libraries(test_probe_lines PRIVATE orbsim_abort_behaviour orbsim_test_support)
catch_discover_tests(test_probe_lines
        PROPERTIES FIXTURES_REQUIRED probe_data LABELS gpu RESOURCE_LOCK gpu)
add_test(NAME probe_lines_golden
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DPROBE=lines
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/probes-golden
                -DGOLDEN_DIR=${ORBSIM_GOLDEN_DIR}
                -DPLANT_DIFF=ON
                "-DEXPECT_STDOUT=: matches"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
set_tests_properties(probe_lines_golden PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# The grid probes (M1-20; register decisions 319-336). All eleven are in the
# list above, so each is rendered without a golden and sets up probe_data;
# test_probe_grid then holds grid-400km's equator, prime meridian and grid to
# the pixels the pinhole camera and the Skyfield fixture put them on, and the
# jitter frames' crossing to its predicted movement, 0.0333 px a step
# (decision 319). grid-400km has a golden as well, compared on a render of its
# own as `lines`' is: one per card, so a card with none fails this test by
# name and leaves the number checks running (decision 331). The jitter frames
# have none: they are judged by number, and by the owner flipping through them.
add_executable(test_probe_grid tests/test_probe_grid.cpp)
target_link_libraries(test_probe_grid PRIVATE orbsim_abort_behaviour orbsim_test_support)
catch_discover_tests(test_probe_grid
        PROPERTIES FIXTURES_REQUIRED probe_data LABELS gpu RESOURCE_LOCK gpu)
add_test(NAME probe_grid-400km_golden
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DPROBE=grid-400km
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/probes-golden
                -DGOLDEN_DIR=${ORBSIM_GOLDEN_DIR}
                -DPLANT_DIFF=ON
                "-DEXPECT_STDOUT=: matches"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
set_tests_properties(probe_grid-400km_golden PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# The Catch2 suites outside ORBSIM_TESTS: today the five above that read the
# probes' frames. `check` builds them, and `mutant-anchors` waits for them, so
# that a suite is named once for both (register decisions 653 and 657).
set(ORBSIM_APP_TESTS test_probe_clear test_radiometry test_tonemap_port test_probe_lines test_probe_grid)

# A golden mismatch, caught end to end (M1-17, register decision 236).
#
# make_broken_goldens writes three altered copies of the run's own `clear`
# frame, halved, into the build tree (since M1-110, register decision 290;
# the committed golden until then) -- a 16x16 block moved by 8 steps, which only the cap
# catches; every value moved by 1 step, which only the mean catches; and a
# 320x180 crop, which cannot be compared at all. It links orbsim_view and no
# Vulkan. The probe is then run against each, and against a golden that does
# not exist, and each run must end as decision 230 says: exit 4 with the diff
# image and both numbers on stderr for a mismatch, exit 1 with the reason for
# a golden that cannot be compared -- and in every case the five usual files,
# which is decision 193's permanent test of a failing run's artefacts.
add_executable(make_broken_goldens tests/make_broken_goldens.cpp)
target_link_libraries(make_broken_goldens PRIVATE orbsim_view orbsim_warnings orbsim_sanitizers)
set(ORBSIM_BROKEN_GOLDEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/golden-broken")
add_test(NAME broken_goldens
        COMMAND make_broken_goldens ${ORBSIM_PROBE_DIR}/clear.png ${ORBSIM_BROKEN_GOLDEN_DIR})
set_tests_properties(broken_goldens PROPERTIES
        FIXTURES_SETUP broken_goldens FIXTURES_REQUIRED probe_clear_frame)

function(orbsim_add_golden_test name golden expect_exit)
    add_test(NAME probe_golden_${name}
            COMMAND ${CMAKE_COMMAND}
                    -DORBSIM=$<TARGET_FILE:orbsim>
                    -DPROBE=clear
                    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/probes-golden-${name}
                    -DGOLDEN=${golden}
                    -DEXPECT_EXIT=${expect_exit}
                    ${ARGN}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
    set_tests_properties(probe_golden_${name} PROPERTIES LABELS gpu RESOURCE_LOCK gpu)
    # Only an altered golden needs make_broken_goldens to have run first.
    string(FIND "${golden}" "${ORBSIM_BROKEN_GOLDEN_DIR}/" in_broken_dir)
    if(in_broken_dir EQUAL 0)
        set_tests_properties(probe_golden_${name} PROPERTIES FIXTURES_REQUIRED broken_goldens)
    endif()
endfunction()
#
# **Which limit each breaks, and the two numbers exactly** (register decision
# 296, amending 275). The altered goldens are made from the run's own frame,
# which the probe renders again and halves the same way, so the alteration is
# the whole difference on every graphics card: the block 8/255 at most and
# 6,144 over 691,200 values on average, the shift 1/255 everywhere. From
# 2026-10-03 until M1-110 they were made from the committed golden, and the
# numbers held the card's own distance from it as well -- on the RX 7900 XTX
# up to 1/255, mean 0.01780/255 -- so only the verdicts were checked.
orbsim_add_golden_test(block ${ORBSIM_BROKEN_GOLDEN_DIR}/clear-block.png 4
        -DEXPECT_DIFF=ON
        "-DEXPECT_STDERR=largest difference 8/255, over the limit of 4/255|mean difference 0.00889/255, within the limit of under 0.5/255")
orbsim_add_golden_test(shift ${ORBSIM_BROKEN_GOLDEN_DIR}/clear-shift.png 4
        -DEXPECT_DIFF=ON
        "-DEXPECT_STDERR=largest difference 1/255, within the limit of 4/255|mean difference 1.00000/255, over the limit of under 0.5/255")
orbsim_add_golden_test(missing ${CMAKE_CURRENT_BINARY_DIR}/no-such-golden.png 1
        "-DEXPECT_STDERR=cannot compare with the golden|no-such-golden.png")
orbsim_add_golden_test(wrong_size ${ORBSIM_BROKEN_GOLDEN_DIR}/clear-wrong-size.png 1
        "-DEXPECT_STDERR=the golden is 320x180 and the frame halves to 640x360")

# A card with no golden (M1-110, register decision 287): --golden-dir pointed
# at a directory holding no card's folder, which must end in exit 1 -- never a
# comparison skipped (VERIFICATION.md rule 23) -- with the five files written,
# no diff image, and on stderr the card named by the numbers in its own
# sidecar (EXPECT_CARD) and the command that approves one. Nothing writes the
# directory; a golden accepted into it by hand would end the run with 0, and
# fail this test, rather than pass it.
set(ORBSIM_NO_CARD_GOLDEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/golden-no-card")
add_test(NAME probe_golden_no_card
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DPROBE=clear
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/probes-golden-no-card
                -DGOLDEN_DIR=${ORBSIM_NO_CARD_GOLDEN_DIR}
                -DEXPECT_EXIT=1
                -DEXPECT_CARD=ON
                "-DEXPECT_STDERR=no golden for this graphics card|golden-no-card|does not exist|--accept-golden"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunProbe.cmake)
set_tests_properties(probe_golden_no_card PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# What --golden, --accept-golden and their neighbours refuse, each a usage
# error with its reason (M1-17, register decisions 232 and 233). No device is
# made: the arguments are refused first. `--accept-golden` never appears here
# beside `--golden`, which the CTest test accept_golden refuses (decision 233).
function(orbsim_add_usage_test name arguments expect)
    add_test(NAME usage_${name}
            COMMAND ${CMAKE_COMMAND}
                    -DORBSIM=$<TARGET_FILE:orbsim>
                    "-DARGS=${arguments}"
                    "-DEXPECT=${expect}"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunUsage.cmake)
endfunction()
# The expected texts leave the other flag out: accept_golden refuses any
# command line that names both, and a message is part of the command line.
orbsim_add_usage_test(accept_without_golden "--probe|clear|--accept-golden"
        "the golden to write")
orbsim_add_usage_test(accept_without_validation "--probe|clear|--accept-golden|--no-validate"
        "--no-validate cannot go with it")
orbsim_add_usage_test(golden_without_probe "--golden|x.png"
        "need --probe <name>")
# --golden-dir as --golden, and never beside it (M1-110, register decision 295).
orbsim_add_usage_test(golden_dir_without_probe "--golden-dir|x"
        "need --probe <name>")
orbsim_add_usage_test(golden_and_golden_dir "--probe|clear|--golden|x.png|--golden-dir|x"
        "give one")

# The benchmark's options, refused by name before a window exists (M1-22,
# register decisions 369-372).
orbsim_add_usage_test(bench_option_without_bench "--frames|10" "need --bench <path>")
orbsim_add_usage_test(bench_unknown_path "--bench|nowhere" "the paths are: grid-orbit")
orbsim_add_usage_test(bench_unknown_quality "--bench|grid-orbit|--quality|best"
        "the presets are: low, medium, high, ultra")
orbsim_add_usage_test(bench_zero_frames "--bench|grid-orbit|--frames|0"
        "--frames needs a whole number from 1 to 1000000, got '0'")
orbsim_add_usage_test(bench_frames_not_a_number "--bench|grid-orbit|--frames|ten" "got 'ten'")
orbsim_add_usage_test(bench_too_wide "--bench|grid-orbit|--width|16385"
        "--width needs a whole number from 1 to 16384")
orbsim_add_usage_test(bench_with_seconds "--bench|grid-orbit|--seconds|2"
        "--seconds does not apply")
orbsim_add_usage_test(bench_with_probe "--bench|grid-orbit|--probe|clear"
        "--bench and --probe cannot go together")
orbsim_add_usage_test(probe_with_bench "--probe|clear|--bench|grid-orbit"
        "--bench and --probe cannot go together")

# The window's scene (the phase A gate, register decision 409): a name it
# knows, and only for the window.
orbsim_add_usage_test(scene_unknown "--scene|earth" "the scenes are: grid, none")
orbsim_add_usage_test(scene_with_probe "--probe|clear|--scene|none"
        "--scene is the window's; it does not apply to --probe or --bench")
orbsim_add_usage_test(scene_with_bench "--bench|grid-orbit|--scene|none"
        "--scene is the window's; it does not apply to --probe or --bench")

# The benchmark, run briefly under the validation layers (M1-22, register
# decision 372): the timestamp queries, the swapchain that does not wait for
# the display and the report, checked on every run of `check` -- and no frame
# time asserted, which register decision 5 rules out. cmake/RunBench.cmake
# says what it checks.
add_test(NAME orbsim_bench_smoke
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/bench-smoke
                -DFRAMES=30
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBench.cmake
)
set_tests_properties(orbsim_bench_smoke PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# A build tree whose shaders are missing is reported, by path, and the
# application exits 1 rather than starting (M1-13, register decision 148). It
# needs a device to get as far as loading shaders, so it is `gpu` too. The
# script checks the exit code and the message, which no single CTest property
# does; it says why.
add_test(NAME shader_missing_is_reported
        COMMAND ${CMAKE_COMMAND}
                -DORBSIM=$<TARGET_FILE:orbsim>
                -DMISSING=${CMAKE_CURRENT_BINARY_DIR}/no-such-shader-directory
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyMissingShader.cmake
)
set_tests_properties(shader_missing_is_reported PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

# The application's own record of the shader files a run asked for (M1-131,
# item 5; register decisions 629, 637 and 642), which the test driver
# fingerprints a test on in place of every compiled shader. Written in every
# tree, the hook on or not, so checked in every tree that builds the
# application: the `clear` probe's record names exactly its three shaders, a
# usage run's none, each ending with its `end` line, and without
# ORBSIM_COVERAGE_RECORDS there is none. scripts/check-coverage-recorder.py
# says what it requires; `gpu`, since the probe needs a device.
if(ORBSIM_PYTHON)
    add_test(NAME shader_record
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-coverage-recorder.py
                    --application $<TARGET_FILE:orbsim>
                    --shaders ${SHADER_OUT_DIR}
    )
else()
    add_test(NAME shader_record COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(shader_record PROPERTIES LABELS gpu RESOURCE_LOCK gpu)

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
        astro/EarthOrientation.hpp
        astro/Sun.hpp
        astro/Tdb.hpp
        core/Attributes.hpp
        core/Contract.hpp
        core/Scalar.hpp
        core/Units.hpp
        core/DoubleDouble.hpp
        core/Math.hpp
        core/LeapSeconds.hpp
        core/Time.hpp
        orbit/Orbit.hpp
)
orbsim_add_header_selfcheck(orbsim_view_header_selfcheck orbsim_view
        view/BenchmarkPath.hpp
        view/Camera.hpp
        view/CameraController.hpp
        view/CameraInput.hpp
        view/CameraPath.hpp
        view/Exposure.hpp
        view/FileWrite.hpp
        view/Frame.hpp
        view/FrameStatistics.hpp
        view/GoldenPath.hpp
        view/GpuClock.hpp
        view/Half.hpp
        view/ImageCompare.hpp
        view/ImageFiles.hpp
        view/Lambert.hpp
        view/LineBatch.hpp
        view/Mat4.hpp
        view/PlanetaryGrid.hpp
        view/Pose.hpp
        view/Projection.hpp
        view/ProbeGradient.hpp
        view/ProbeImage.hpp
        view/ProbeSidecar.hpp
        view/PushConstants.hpp
        view/RenderQuality.hpp
        view/SceneClear.hpp
        view/Srgb.hpp
        view/Tonemap.hpp
        view/VertexLayout.hpp
)
orbsim_add_header_selfcheck(orbsim_test_header_selfcheck orbsim_test_support
        tests/AbortProbe.hpp
        tests/CountWraparound.hpp
        tests/FixtureFile.hpp
        tests/HdrDumpFile.hpp
        tests/OrbitSweepSupport.hpp
        tests/OrbitTestSupport.hpp
        tests/TimeTestSupport.hpp
        tests/WriterTurns.hpp
)
add_dependencies(test_orbit orbsim_header_selfcheck orbsim_test_header_selfcheck)
add_dependencies(test_view_math orbsim_view_header_selfcheck)
add_dependencies(test_projection orbsim_view_header_selfcheck)
add_dependencies(test_camera orbsim_view_header_selfcheck)
add_dependencies(test_camera_controller orbsim_view_header_selfcheck)
add_dependencies(test_camera_path orbsim_view_header_selfcheck)
add_dependencies(test_render_quality orbsim_view_header_selfcheck)
add_dependencies(test_pipeline_inputs orbsim_view_header_selfcheck)
add_dependencies(test_srgb orbsim_view_header_selfcheck)
add_dependencies(test_exposure orbsim_view_header_selfcheck)
add_dependencies(test_tonemap orbsim_view_header_selfcheck)
add_dependencies(test_half orbsim_view_header_selfcheck)
add_dependencies(test_hdr_dump orbsim_view_header_selfcheck)
add_dependencies(test_image_files orbsim_view_header_selfcheck)
add_dependencies(test_probe_sidecar orbsim_view_header_selfcheck)
add_dependencies(test_image_compare orbsim_view_header_selfcheck)
add_dependencies(test_golden_path orbsim_view_header_selfcheck)
add_dependencies(test_line_batch orbsim_view_header_selfcheck)
add_dependencies(test_planetary_grid orbsim_view_header_selfcheck)
add_dependencies(test_statistics orbsim_view_header_selfcheck)
add_dependencies(test_gpu_clock orbsim_view_header_selfcheck)
add_dependencies(test_benchmark_path orbsim_view_header_selfcheck)
add_dependencies(test_file_replace orbsim_view_header_selfcheck)

if(ORBSIM_BUILD_APP)
    orbsim_add_header_selfcheck(orbsim_render_header_selfcheck orbsim_render_deps
            render/LineRenderer.hpp
            render/Pipeline.hpp
            render/Probes.hpp
            render/ResolvePass.hpp
            render/VulkanHandle.hpp
            render/VulkanContext.hpp
            app/ExitCodes.hpp
            app/InteractiveView.hpp
            app/PathFromUtf8.hpp
            app/ProbeMode.hpp
            app/SdlHandle.hpp
            app/ShaderRecord.hpp
    )
    add_dependencies(orbsim orbsim_render_header_selfcheck)
endif()

# ---------------------------------------------------------------------------
# The definition of done.
#
# `cmake --build <tree> --target check` is what "finished" means here:
# everything builds, every test passes, clang-tidy's configuration names only
# checks that exist and then finds nothing,
# clang-format would change nothing, and no document links -- or points at in
# backticks -- something that is not there. CLAUDE.md points at this target.
# Run it in both
# build/relwithdebinfo and build/debug -- the assertions are only live in the
# second.
#
# lint is one stamp per translation unit, so Ninja runs clang-tidy in
# parallel and re-lints only what changed. Every header is a dependency of
# every stamp, which is correct (a header change can surface anywhere) and
# still far cheaper than the serial run it replaces.
# ---------------------------------------------------------------------------
find_program(ORBSIM_CLANG_TIDY clang-tidy)
find_program(ORBSIM_CLANG_FORMAT clang-format)

# Every file of the repository this tree compiles is in this list or in
# ORBSIM_LINT_EXCEPTIONS, with its reason, beside its target; the CTest test
# lint_deps fails otherwise (M1-135).
set(ORBSIM_LINT_SOURCES
        ${ORBSIM_CORE_SOURCES} ${ORBSIM_TEST_SUPPORT_SOURCES} ${ORBSIM_FUZZ_SOURCES})
foreach(test_name IN LISTS ORBSIM_TESTS)
    list(APPEND ORBSIM_LINT_SOURCES tests/${test_name}.cpp)
endforeach()
# The two kinds of test source that are not a suite's own file, and were not
# linted until 2026-09-24: the abort listener compiled into every suite, and
# the count-wraparound probes. Found when the call they both make reached
# src/app/main.cpp and met a lint conflict there that it had never met here
# (register decision 156).
list(APPEND ORBSIM_LINT_SOURCES tests/AbortBehaviour.cpp)
# The read-record listener compiled into every suite beside it, compiled since
# M1-129 and never linted until M1-131 found it (register decision 650).
list(APPEND ORBSIM_LINT_SOURCES tests/ReadRecordListener.cpp)
# The render-side maths library's own sources, which were not linted until
# 2026-09-25: the list named the core, test and application sources and not
# these, so src/view/Camera.cpp had never been through clang-tidy since M1-11
# added it. Found by M1-15; added on the owner's ruling (register decision 185).
list(APPEND ORBSIM_LINT_SOURCES ${ORBSIM_VIEW_SOURCES} ${ORBSIM_STB_IMAGE_SOURCES})
foreach(operation IN LISTS ORBSIM_COUNT_WRAPAROUND_OPERATIONS)
    list(APPEND ORBSIM_LINT_SOURCES tests/count_wraparound_${operation}.cpp)
endforeach()
list(APPEND ORBSIM_LINT_SOURCES tests/angle_between_zero_probe.cpp tests/assertion_message_probe.cpp
        tests/reproducible_build_probe.cpp)
if(ORBSIM_BUILD_APP)
    list(APPEND ORBSIM_LINT_SOURCES ${ORBSIM_APP_SOURCES} tests/test_probe_clear.cpp
            tests/test_radiometry.cpp tests/test_tonemap_port.cpp tests/make_broken_goldens.cpp
            tests/test_probe_lines.cpp tests/test_probe_grid.cpp)
endif()
# The coverage recorder and its self-test's two sources (M1-131), where the
# hook is on and they are compiled.
if(ORBSIM_COVERAGE_HOOK)
    list(APPEND ORBSIM_LINT_SOURCES tests/CoverageRecorder.cpp tests/coverage_recorder_probe.cpp
            tests/coverage_recorder_unaligned.cpp)
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

# The memory pool and the lint steps that run in it (M1-88, M1-96):
# cmake/MemoryPool.cmake. Its compile half is called last in this file.
orbsim_configure_input("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MemoryPool.cmake")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MemoryPool.cmake)

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

# Documentation is checked too, because a reference that has rotted is
# invisible from the inside: the file is still on disk and still says the
# words, and only following the link finds out. A survey of 118 repositories
# put 59% of them in that state. The script proves it can fail before it is
# trusted -- see its own docstring.
if(ORBSIM_PYTHON)
    add_custom_target(doc-links
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-links.py
                    ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "doc-links: every Markdown link, anchor and backticked path"
            VERBATIM
    )
else()
    # Same rule as lint and format-check: a check that cannot run must fail
    # rather than pass by doing nothing.
    add_custom_target(doc-links
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "python was not found on PATH; doc-links cannot run"
            VERBATIM
    )
endif()

# The mutation harness's anchors (VERIFICATION.md rule 19, scripts/mutate.py).
# A pass itself is periodic and far too slow for `check`: it rebuilds the tree
# once per mutant. But the anchors rot silently as the code moves -- a mutant
# whose `find` string no longer matches is a mutant that cannot run, and a
# mutant that cannot run is not a kill, which is the trap M1-06 recorded five
# of. Verifying them costs milliseconds, and the argument is doc-links': a
# reference that has rotted is invisible from the inside. The script fails on
# an empty directory rather than passing, by the same rule as below.
if(ORBSIM_PYTHON)
    # Every CTest judge a mutant names is checked against this tree's tests
    # where the tree builds the application (M1-129, register decision 523): a
    # core-only tree has neither the GPU tests nor the mutants_due ones.
    set(orbsim_judge_tree)
    if(ORBSIM_BUILD_APP)
        set(orbsim_judge_tree --tree ${CMAKE_BINARY_DIR})
    endif()
    add_custom_target(mutant-anchors
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutate.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mutants --verify ${orbsim_judge_tree}
            COMMENT "mutant-anchors: every mutation-pass anchor still matches once, every judge exists"
            VERBATIM
    )
    # The judges are checked against CTest's list of this tree's tests, and a
    # Catch2 suite's part of that list is written after the suite links, by
    # its POST_BUILD discovery. Read while a suite was relinked, the list
    # lacked its tests, and two judges were reported "not a test in this
    # tree" (register decision 653). So the step waits for every suite whose
    # tests are discovered -- each one catch_discover_tests is called for;
    # the rest of the list is written by the configure.
    if(ORBSIM_BUILD_APP)
        add_dependencies(mutant-anchors ${ORBSIM_TESTS} ${ORBSIM_APP_TESTS})
    endif()
else()
    add_custom_target(mutant-anchors
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "python was not found on PATH; mutant-anchors cannot run"
            VERBATIM
    )
endif()

# The tests of this project's own scripts and build, from the mutation
# harness's self-test to the lint dependencies: cmake/ScriptTests.cmake.
orbsim_configure_input("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScriptTests.cmake")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScriptTests.cmake)

# The Horizons converter's golden test (M1-06). The converter sits in the
# provenance chain of every Horizons fixture, so it is tested in `check` like
# anything else: a synthetic response must convert to a known text, convert
# identically when its timestamp and Earth-orientation lines change, and be
# refused when it is in the wrong units, frame or corrections. Python is
# already required by doc-links; without it this fails rather than passing by
# doing nothing, by the same rule.
if(ORBSIM_PYTHON)
    add_test(NAME horizons_fixture_converter
            COMMAND ${ORBSIM_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/horizons-fixture.py --self-test
    )
else()
    add_test(NAME horizons_fixture_converter COMMAND ${CMAKE_COMMAND} -E false)
endif()
set_tests_properties(horizons_fixture_converter PROPERTIES LABELS fixtures)

# `check` runs the tests whose inputs changed since their last passing run,
# and `check-full` runs every one (M1-129, register decisions 483, 484, 496
# and 503): scripts/run-tests.py decides from fingerprints which tests to hand
# to CTest, prints how many it skipped, and after a full run from a clean
# working tree stamps the commit, which the pre-push hook requires in both
# trees. A plain `ctest` still runs everything it is given. Python is already
# required by doc-links; without it both fail rather than passing by doing
# nothing.
if(ORBSIM_PYTHON)
    set(orbsim_run_tests ${ORBSIM_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-tests.py
            ${CMAKE_BINARY_DIR} --ctest ${CMAKE_CTEST_COMMAND} --jobs ${ORBSIM_TEST_JOBS})
else()
    set(orbsim_run_tests ${CMAKE_COMMAND} -E false)
endif()
add_custom_target(check
        COMMAND ${orbsim_run_tests}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "run-tests: the suites whose inputs changed, and the GPU tests where there is a GPU"
        VERBATIM
)
add_custom_target(check-full
        COMMAND ${orbsim_run_tests} --full
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "run-tests --full: every test, none skipped, and the commit stamped for the push"
        VERBATIM
)
set(ORBSIM_CHECK_DEPENDENCIES
        ${ORBSIM_TESTS}
        count_wraparound_subtraction
        count_wraparound_addition
        count_wraparound_multiplication
        angle_between_zero_probe
        assertion_message_probe
        reproducible_build_probe
        t_erfa_c
        t_erfa_c_extra
        orbsim_header_selfcheck
        orbsim_test_header_selfcheck
        orbsim_view_header_selfcheck
        # The fuzz sources, compiled under the full warning set. Not in this
        # list until 2026-09-13, so a warning there failed `ninja` and passed
        # `check` -- see the comment beside the object library above.
        orbsim_fuzz_objects
        lint
        format-check
        doc-links
        mutant-anchors
        configure-current
)
# The coverage recorder's self-test programs (M1-131), where the hook is on.
if(ORBSIM_COVERAGE_HOOK)
    list(APPEND ORBSIM_CHECK_DEPENDENCIES coverage_recorder_probe coverage_recorder_unaligned
            coverage_recorder_probe_plain)
endif()
add_dependencies(check ${ORBSIM_CHECK_DEPENDENCIES})
add_dependencies(check-full ${ORBSIM_CHECK_DEPENDENCIES})
if(ORBSIM_BUILD_APP)
    # The application with the suites that check it, ORBSIM_APP_TESTS: a suite
    # `check` does not build is one ctest reports as NOT_BUILT (M1-16, found
    # the first time a fresh tree ran it) -- test_probe_clear, the two suites
    # that read the numeric probes' frames (M1-18), the `lines` probe's number
    # check (M1-19) and the grid probes' (M1-20). make_broken_goldens for the
    # golden tests' fixture (M1-17).
    foreach(orbsim_check IN ITEMS check check-full)
        add_dependencies(${orbsim_check} orbsim orbsim_render_header_selfcheck make_broken_goldens
                ${ORBSIM_APP_TESTS})
    endforeach()
endif()

# Every library and program this file defines compiles in the memory pool
# (M1-96): last, so that every target exists. cmake/MemoryPool.cmake says why.
orbsim_pool_top_level_compiles()

# The coverage hook, on every library and program this file defines but those
# compiling someone else's code (M1-131, register decisions 545, 559, 569, 582,
# 588, 639 and 644): last, beside the pool, so that every target exists, a
# target added later among them. With it on:
#
#   * the hook on C++ alone, with -ffunction-sections and -fdata-sections, so
#     that each function and each piece of data is a section of its own to be
#     told apart by (decision 545);
#   * every program linked with lld-link's map, `/map`, which writes it beside
#     the program and turns a recorded address into a function (decision 582);
#   * the recorder linked into every program carrying the hook; one carrying
#     the hook without it does not link (decision 626).
#
# scripts/check-coverage-hook.py checks what they made, object by object.
if(ORBSIM_COVERAGE_HOOK)
    set(orbsim_unhooked_targets
            # Someone else's code (decision 588): ERFA's library and its two
            # validation programs, lodepng, stb_image's decoder and VMA. They
            # change only with a version pin, and go by their binaries.
            orbsim_erfa t_erfa_c t_erfa_c_extra orbsim_lodepng orbsim_stb_image vma_impl
            # The recorder, which the hook calls (decision 566), and the
            # self-test's plain program, built without either (decision 636).
            orbsim_coverage_recorder coverage_recorder_probe_plain
            # The header self-checks, compiled from files CMake generates
            # rather than from src/ or tests/ (decision 644).
            orbsim_header_selfcheck orbsim_view_header_selfcheck orbsim_test_header_selfcheck
            orbsim_render_header_selfcheck)
    set(orbsim_coverage_hook_flags
            -fsanitize-coverage=func,inline-bool-flag,pc-table -ffunction-sections -fdata-sections)
    get_property(orbsim_top_level_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(orbsim_target IN LISTS orbsim_top_level_targets)
        get_target_property(orbsim_target_type ${orbsim_target} TYPE)
        if(orbsim_target_type STREQUAL "EXECUTABLE")
            target_link_options(${orbsim_target} PRIVATE LINKER:/map)
        endif()
        if(NOT orbsim_target_type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$"
                OR orbsim_target IN_LIST orbsim_unhooked_targets)
            continue()
        endif()
        target_compile_options(${orbsim_target} PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:${orbsim_coverage_hook_flags}>")
        if(orbsim_target_type STREQUAL "EXECUTABLE")
            target_link_libraries(${orbsim_target} PRIVATE orbsim_coverage_recorder)
        endif()
    endforeach()
endif()

# Every test this file defines carries its own name in its environment, as
# ORBSIM_TEST_NAME, in every tree (M1-131, register decisions 564 and 594): a
# test run through a script starts programs whose command lines are not its
# own, and each writes the name into its coverage record, so that the records
# of one test can be found. Last, after every test is defined, so that a test
# added later cannot be missed; the tests Catch2 discovers in each suite are
# matched by their command lines instead, and are not among them.
# scripts/check-coverage-hook.py holds every one to it.
get_property(orbsim_defined_tests DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY TESTS)
foreach(orbsim_test IN LISTS orbsim_defined_tests)
    set_property(TEST ${orbsim_test} APPEND PROPERTY ENVIRONMENT "ORBSIM_TEST_NAME=${orbsim_test}")
endforeach()

# Every static library this file defines carries the Debug postfix from the
# first configure on (M1-109, register decision 395): a target takes it from
# CMAKE_DEBUG_POSTFIX as it is created, so one declared before the line that
# sets it would be named differently by a tree's first configure and by its
# second -- which is what Imath's cache entry did until then. Checked only
# where Debug is built: CMake gives a target the property only for the
# configurations a tree builds (measured with CMake 4.3.1 on 2026-10-05:
# `_d` in a Debug tree, not set at all in a RelWithDebInfo one).
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES)
    get_directory_property(orbsim_defined_targets BUILDSYSTEM_TARGETS)
    foreach(orbsim_target IN LISTS orbsim_defined_targets)
        get_target_property(orbsim_target_type ${orbsim_target} TYPE)
        if(orbsim_target_type STREQUAL "STATIC_LIBRARY")
            get_target_property(orbsim_target_postfix ${orbsim_target} DEBUG_POSTFIX)
            if(NOT orbsim_target_postfix STREQUAL "_d")
                message(FATAL_ERROR "${orbsim_target} is not given the Debug postfix _d: it "
                        "was declared before CMAKE_DEBUG_POSTFIX is set, so a tree's first "
                        "configure would name it differently from its second (M1-109)")
            endif()
        endif()
    endforeach()
endif()

# Last of all, once everything this configure reads has been read: fail if any
# of it changed while the configure ran, and record the fingerprints for
# `configure-current` (M1-98, register decision 225).
orbsim_configure_inputs_unchanged("${CMAKE_BINARY_DIR}/orbsim-configured-from.sha256")
