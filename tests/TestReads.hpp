#ifndef ORBSIM_TESTS_TESTREADS_HPP
#define ORBSIM_TESTS_TESTREADS_HPP
//
// The files a test case reads, named here and recorded here (M1-129, register
// decisions 489, 498 and 527-530).
//
// **Why.** `check` skips a test whose program, command line, environment and
// every file it read are what they were at its last passing run
// (scripts/run-tests.py). A file the driver does not know the test read is a
// file whose change it cannot see. So a test never builds the path of a file
// it reads from a folder of its own: it asks this header for the one file,
// and the asking is the recording. The folders themselves -- data/ and the
// probes' output folder -- are compiled into tests/TestReads.cpp alone, and
// the CTest test `test_reads` fails if any other file under tests/ names them
// or opens a file itself (decision 530).
//
// **The record.** As each test case ends, the listener in
// tests/ReadRecordListener.cpp writes `test-reads/<test name>.txt` in the build
// tree: one line per file, its SHA-256 and its path, the hash taken then, so
// that a later test rewriting the file cannot hide the version this one read
// (decision 527) -- `missing` in place of the hash for a file that was not
// there -- and a last line `end`, without which the record counts as cut off.
// A test that read nothing writes a record with no file in it, which is what
// lets the driver tell "read nothing" from "does not record".
//
// **Views, not paths.** Each function below returns a view of text this file
// keeps for the life of the program, never a std::filesystem::path or a
// std::string: libstdc++ tags both with its "cxx11" ABI, and a function
// returning one draws gcc's -Wabi-tag (measured with g++-14 on 2026-10-14).
// The view never dangles; a caller builds a path from it at no cost.
//
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace orb::test {

// data/<relative> in the source tree, recorded as read by the running test
// case. `relative` uses forward slashes: "skyfield/tdb-minus-tt.txt".
[[nodiscard]] std::string_view dataFile(std::string_view relative);

// <the probes' folder>/<name> in the build tree, where the probe tests write
// their images, recorded as read: "clear.hdr.f32".
[[nodiscard]] std::string_view probeFile(std::string_view name);

// One name per way reading a file through this header can fail.
enum class FileReadError : std::uint8_t {
    NotRecorded,    // not a path dataFile() or probeFile() handed out
    OutsideScratch, // not under the system's temporary directory
    CannotOpen,     // no readable file at that path
};

[[nodiscard]] constexpr std::string_view describe(FileReadError error) noexcept {
    switch (error) {
    case FileReadError::NotRecorded:
        return "the path was not handed out by dataFile() or probeFile(), so it is not recorded";
    case FileReadError::OutsideScratch:
        return "the file is not under the system's temporary directory";
    case FileReadError::CannotOpen:
        return "there is no readable file at that path";
    }
    return "unknown file read error";
}

// The bytes of a file dataFile() or probeFile() named, read whole. Any other
// path is refused, checked character for character against what they handed
// out, so this cannot become a way to read a file unrecorded (decision 530).
[[nodiscard]] std::expected<std::vector<std::byte>, FileReadError>
readRecordedFile(std::string_view file);

// The bytes of a file a test wrote itself under the system's temporary
// directory, read back whole. Not recorded: what a test wrote is its own
// output, not an input. Anything outside that directory is refused.
[[nodiscard]] std::expected<std::vector<std::byte>, FileReadError>
readScratchFile(const std::filesystem::path& file);

// The listener's two calls (tests/ReadRecordListener.cpp): a test case named
// `testName` begins, and the one that began has ended, so its record is
// written. Not for the tests themselves.
void startReadRecord(std::string_view testName);
void finishReadRecord();

} // namespace orb::test

#endif // ORBSIM_TESTS_TESTREADS_HPP
