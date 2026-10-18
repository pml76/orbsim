//
// The coverage recorder: which functions a program entered (M1-131, register
// decisions 559, 564, 566, 570, 626, 627, 632, 634, 637, 638 and 644).
//
// **What it records.** With `ORBSIM_COVERAGE_HOOK` on, every function of this
// project's own code is compiled with clang's
// `-fsanitize-coverage=func,inline-bool-flag,pc-table`: each function sets a
// flag of its own when it is entered, with no call, and the compiler keeps a
// table of every function's address beside the flags (decision 559). This file
// supplies what the hook needs from the program -- the four section markers
// lld-link does not, and the two start-up calls through which the hook hands
// over its flags and its table -- and writes down, once, which flags are set.
// M1-132 reads the record to choose the tests a change can affect. It is
// linked into every program carrying the hook, the application included
// (decisions 566 and 639), and compiled without it.
//
// **Where, and only when asked.** Only when `ORBSIM_COVERAGE_RECORDS` names a
// folder, which the test driver sets for its run and empties before it
// (decision 570): a program started any other way writes nothing. One file a
// process, `<process number>-<start time in ns>.functions.txt` -- named after
// the number alone, Windows' reuse of process numbers overwrote 38 of 560
// records (decision 564); the number from the C runtime, the time from the C++
// clock at the hook's first start-up call (decision 627).
//
// **What a record says** (decision 644), one item a line, in UTF-8:
//
//     program <the program's path>
//     command <its command line>
//     test <ORBSIM_TEST_NAME, which may be empty>
//     ended normally | by abort | by a crash | by a bad argument
//     flags <count>               or   flags <count> out of line
//     <an entered function's address less the program's base, in hexadecimal>
//     ...
//     end <the number of function lines>
//
// The flags are checked to line up with the table, and the pointers the hook
// handed over to match this file's own markers, before any function is
// written: where they do not, a flag cannot be placed beside its function, so
// the count is written "out of line" and no function at all (decisions 559 and
// 627). The `end` line comes last, so that a record cut off part-way -- a crash
// inside the write, a full disk, which a file stream meets without a word --
// has none, and counts as no record rather than as a record of fewer functions
// (decision 637).
//
// **When.** The hook's first start-up call registers the writer with `atexit`.
// clang places that call in `.CRT$XCA00002`, which runs before the standard
// library's initialisers and every ordinary one, and the C library calls the
// functions `atexit` was given newest first -- so the writer runs after the
// destructor of every global object, and records what they enter (decision
// 626, measured 2026-10-17). The same call registers three handlers that write
// the record when the program stops abnormally and then let it end exactly as
// it would have, its exit code and its output unchanged (decision 634):
//
//   * `abort` -- which a failed ORBSIM_EXPECTS or ORBSIM_ENSURES calls, and
//     std::terminate -- raises SIGABRT, and abort ends the program as before
//     once the handler returns;
//   * a crash, the debug standard library's checks among them, which end as an
//     illegal instruction: a handler Windows calls for every exception before
//     any of the program's own, acting only on the kinds that end a program and
//     handing each on. Catch2 replaces Windows' last-chance filter during every
//     test case and hands a crash to nothing, which is why the handler is of
//     this kind, which runs first;
//   * a bad argument to the C runtime: its invalid-parameter handler, replaced
//     by one that writes and then ends the program with the `__fastfail` the
//     runtime's own handler ends it with.
//
// C++ allows a signal handler very little, and writing a file is not among it;
// on Windows the handler runs in the thread that stopped, and it worked in
// every case measured. `bugprone-signal-handler`, which would judge this, is
// silent in C++17 and later -- measured -- so the handlers rest on that
// measurement and on the self-test, scripts/check-coverage-recorder.py.
//
// **One write at a time** (decision 638). The crash handler acts before any
// code that may catch the exception, so it can write in a program that then
// goes on; a write on another thread then waits for it to finish, and reads the
// flags only once it has its turn, so that the last record written is the
// fullest. A write on the same thread -- a crash inside the write itself --
// gives up at once, and leaves the record its first attempt cut off without an
// `end` line. The wait is bounded (Non-negotiable 4): where the bound runs out,
// something is already stuck, and the program ends as failed, so that its test
// fails and nothing is stored. The turns are tests/WriterTurns.hpp's, a header of
// its own so that tests/test_writer_turns.cpp can prove them with two threads
// held in step (decision 648).
//
#include "tests/WriterTurns.hpp"

#include <atomic>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <process.h>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// The headers below declare what this file uses, and none compiles without
// this one first ("No Target Architecture"); misc-include-cleaner is told it
// is needed (decision 627).
#include <Windows.h> // IWYU pragma: keep

#include <errhandlingapi.h>
#include <excpt.h>
#include <intrin.h>
#include <libloaderapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <processenv.h>
// _set_invalid_parameter_handler is declared in the Windows C runtime's
// <stdlib.h> itself, which misc-include-cleaner requires and
// modernize-deprecated-headers forbids: the one-line suppression
// tests/AbortBehaviour.cpp and src/app/main.cpp carry, by the owner's go-ahead
// (decisions 155 and 634).
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>
#include <winnt.h>

// The sections the hook's flags and table are merged into, each bracketed by a
// marker before and after. lld-link sorts a section's parts by the text after
// the `$`, so `$BA` and `$BZ` fall either side of the compiler's `$BM`, as
// `$A` and `$Z` of its `$M`; it supplies no such markers itself on Windows
// (decision 559).
#pragma section(".SCOV$BA", read, write)
#pragma section(".SCOV$BZ", read, write)
#pragma section(".SCOVP$A", read)
#pragma section(".SCOVP$Z", read)

// The six names the hook dictates: reserved names, which both the compiler and
// clang-tidy refuse, off at their declarations alone by the owner's go-ahead
// (decisions 565 and 627, Non-negotiable 10, working agreement 7).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
extern "C" {
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern std::uint64_t __start___sancov_bools;
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern bool __stop___sancov_bools;
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern const std::uint64_t __start___sancov_pcs;
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern const std::uint64_t __stop___sancov_pcs;
// The hook's two start-up calls. The recorder only reads through the pointers:
// a function of C linkage is found by its name, `const` does not change how a
// pointer is passed, and the compiler is the only caller (decision 627).
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
void __sanitizer_cov_bool_flag_init(const bool* begin, const bool* end);
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
void __sanitizer_cov_pcs_init(const std::uintptr_t* begin, const std::uintptr_t* end);
__declspec(allocate(".SCOV$BA")) std::uint64_t __start___sancov_bools = 0;
__declspec(allocate(".SCOV$BZ")) bool __stop___sancov_bools = false;
__declspec(allocate(".SCOVP$A")) const std::uint64_t __start___sancov_pcs = 0;
__declspec(allocate(".SCOVP$Z")) const std::uint64_t __stop___sancov_pcs = 0;
// The abort handler, of C linkage as a signal handler is to be.
void writeOnAbort(int signal);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace {

// The longest value Windows lets an environment variable hold, and the longest
// path GetModuleFileNameW can return, in wide characters, its terminator
// included (Microsoft's documentation of GetEnvironmentVariableW and of
// maximum path lengths).
constexpr std::size_t kLongestText = 32'768;

// What the hook handed over, and whose turn it is to write. Inside a function
// rather than at namespace scope, so that this file holds no mutable global
// of its own (decision 627).
struct Seen {
    std::span<const bool> flags;
    std::span<const std::uintptr_t> table;
    std::chrono::system_clock::time_point started;
    bool registered = false;
    bool flagsMatchMarkers = false;
    bool tableMatchesMarkers = false;
    // The thread writing a record now, or none.
    std::atomic<std::thread::id> writer;
};

Seen& seen() {
    static Seen value;
    return value;
}

[[nodiscard]] std::uintptr_t addressOf(const void* pointer) {
    return std::bit_cast<std::uintptr_t>(pointer);
}

// Empty where the variable is not set, or is set to nothing.
[[nodiscard]] std::wstring environmentValue(const wchar_t* name) {
    std::wstring value(kLongestText, L'\0');
    const DWORD length =
        GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    value.resize(length < value.size() ? length : 0);
    return value;
}

[[nodiscard]] std::wstring programPath() {
    std::wstring path(kLongestText, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    return path;
}

void writeLine(std::ofstream& out, std::string_view key, std::wstring_view text) {
    const std::u8string utf8 = std::filesystem::path(text).u8string();
    out << key << ' ' << std::string(utf8.begin(), utf8.end()) << '\n';
}

// The record, if one was asked for. Called only by the writer whose turn it
// is, so the flags are read inside that turn.
void writeRecordFile(std::string_view how) {
    const std::wstring folder = environmentValue(L"ORBSIM_COVERAGE_RECORDS");
    if (folder.empty()) return;
    const Seen& state = seen();
    const auto started =
        std::chrono::duration_cast<std::chrono::nanoseconds>(state.started.time_since_epoch());
    const std::filesystem::path file =
        std::filesystem::path(folder) /
        (std::to_string(_getpid()) + "-" + std::to_string(started.count()) + ".functions.txt");
    std::ofstream out(file, std::ios::binary);
    if (!out) return;
    writeLine(out, "program", programPath());
    writeLine(out, "command", GetCommandLineW());
    writeLine(out, "test", environmentValue(L"ORBSIM_TEST_NAME"));
    out << "ended " << how << '\n';
    // Two words to each entry of the table: the function's address, and the
    // hook's own note that it is a function's entry.
    const bool aligned = state.flagsMatchMarkers && state.tableMatchesMarkers &&
                         state.table.size() == 2 * state.flags.size();
    out << "flags " << state.flags.size();
    if (!aligned) {
        out << " out of line\nend 0\n";
        return;
    }
    out << '\n';
    const std::uintptr_t base = addressOf(GetModuleHandleW(nullptr));
    std::size_t entered = 0;
    for (const auto [flag, entry] :
         std::views::zip(state.flags, state.table | std::views::chunk(2))) {
        if (!flag) continue;
        out << std::hex << (entry.front() - base) << std::dec << '\n';
        ++entered;
    }
    out << "end " << entered << '\n';
}

void writeRecord(std::string_view how) noexcept {
    std::atomic<std::thread::id>& writer = seen().writer;
    switch (orb::coverage::takeTurn(writer, orb::coverage::kLongestWait)) {
    case orb::coverage::Turn::Taken:
        break;
    case orb::coverage::Turn::HeldByThisThread:
        return;
    case orb::coverage::Turn::RanOut:
        // Stuck: ended as failed, the way the C runtime ends a program that
        // must not go on (decisions 638 and 651).
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
    try {
        writeRecordFile(how);
    } catch (...) {
        // A write that failed part-way leaves a record without its `end`
        // line, which counts as none (decision 637); nothing can be said from
        // here without changing the program's output (decision 634).
        orb::coverage::giveTurnBack(writer);
        return;
    }
    orb::coverage::giveTurnBack(writer);
}

void writeAtExit() { writeRecord("normally"); }

[[noreturn]] void writeOnInvalidParameter(const wchar_t* /*expression*/,
                                          const wchar_t* /*function*/,
                                          const wchar_t* /*file*/,
                                          unsigned int /*line*/,
                                          std::uintptr_t /*reserved*/) {
    writeRecord("by a bad argument");
    // How the C runtime's own handler ends the program (its _invoke_watson).
    __fastfail(FAST_FAIL_INVALID_ARG);
}

// Called for every exception before any handler of the program's own. Only
// the kinds that end a program are acted on; a C++ exception is an exception
// to Windows too, and is passed by, as every kind is passed on.
LONG WINAPI writeOnCrash(EXCEPTION_POINTERS* crash) {
    const DWORD code = crash->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_PRIV_INSTRUCTION) {
        writeRecord("by a crash");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

extern "C" void writeOnAbort(int /*signal*/) { writeRecord("by abort"); }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
extern "C" void __sanitizer_cov_bool_flag_init(const bool* begin, const bool* end) {
    Seen& state = seen();
    if (state.registered) return;
    state.registered = true;
    // No length exists anywhere: the linker decides it. Off at this line alone,
    // as src/app/main.cpp and src/render/VulkanContext.cpp have it for theirs
    // (decision 627).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    state.flags = std::span<const bool>(begin, end);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    state.started = std::chrono::system_clock::now();
    // The hook hands over the start marker's address plus its eight bytes,
    // and the stop marker's (measured, decision 627).
    state.flagsMatchMarkers =
        addressOf(begin) == addressOf(&__start___sancov_bools) + sizeof(std::uint64_t) &&
        addressOf(end) == addressOf(&__stop___sancov_bools);
    // A registration that fails leaves no record of that ending, which is
    // the safe side: a test that left none is run again.
    static_cast<void>(std::atexit(writeAtExit));
    static_cast<void>(std::signal(SIGABRT, writeOnAbort));
    static_cast<void>(_set_invalid_parameter_handler(writeOnInvalidParameter));
    // First in the list, ahead of any handler registered later.
    static_cast<void>(AddVectoredExceptionHandler(1, writeOnCrash));
}

extern "C" void __sanitizer_cov_pcs_init(const std::uintptr_t* begin, const std::uintptr_t* end) {
    Seen& state = seen();
    if (!state.table.empty()) return;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    state.table = std::span<const std::uintptr_t>(begin, end);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    state.tableMatchesMarkers =
        addressOf(begin) == addressOf(&__start___sancov_pcs) + sizeof(std::uint64_t) &&
        addressOf(end) == addressOf(&__stop___sancov_pcs);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
