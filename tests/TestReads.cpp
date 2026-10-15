#include "tests/TestReads.hpp"

#include "tests/Sha256.hpp"
#include "tests/TestCase.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace orb::test {

namespace {

// --- the record's file name (register decision 528) ---------------------------

// A character Windows forbids in a file name, or `%`, which the escape itself
// uses: written as `%` and two uppercase hexadecimal digits. So is a last `.`
// or space, which Windows drops from a name without a word, so that `x.` and
// `x` would share a record. scripts/run-tests.py escapes the same way,
// written from this description and held to the same table in its self-test.
enum class Position : std::uint8_t { Inside, Last };

[[nodiscard]] constexpr bool needsEscape(char c, Position position) noexcept {
    constexpr std::string_view kForbidden = R"(<>:"/\|?*%)";
    const auto code = static_cast<unsigned char>(c);
    return code < 0x20U || kForbidden.contains(c) ||
           (position == Position::Last && (c == '.' || c == ' '));
}

[[nodiscard]] constexpr std::string escapedName(std::string_view name) {
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name.at(i);
        if (!needsEscape(c, i + 1 == name.size() ? Position::Last : Position::Inside)) {
            out.push_back(c);
            continue;
        }
        const auto code = static_cast<unsigned char>(c);
        out.push_back('%');
        out.push_back(kDigits.at(code >> 4U));
        out.push_back(kDigits.at(code & 0x0fU));
    }
    return out;
}

// The table scripts/run-tests.py's self-test holds its own escape to.
static_assert(escapedName("propagation composes: one long step") ==
              "propagation composes%3A one long step");
static_assert(escapedName("f/16 at 1/125 s") == "f%2F16 at 1%2F125 s");
static_assert(escapedName(R"(a <b> "c" d\e |f? *g)") == "a %3Cb%3E %22c%22 d%5Ce %7Cf%3F %2Ag");
static_assert(escapedName("100% sure") == "100%25 sure");
static_assert(escapedName("ends in a dot.") == "ends in a dot%2E");
static_assert(escapedName("ends in a space ") == "ends in a space%20");
static_assert(escapedName("a.b c") == "a.b c");
static_assert(escapedName("tab\there") == "tab%09here");
static_assert(escapedName("").empty());

// --- SHA-256, proved against its references (tests/Sha256.hpp) --------------

[[nodiscard]] constexpr std::string hexText(const Sha256Digest& digest) {
    const std::array<char, 64> hex = hexOf(digest);
    return {hex.data(), hex.size()};
}

// FIPS 180-2 appendix B's examples, B.1 and B.2, and the empty message.
static_assert(hexText(sha256("abc")) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
static_assert(hexText(sha256("")) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
static_assert(hexText(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
// Python's hashlib on runs of `a` at the lengths where the padding changes
// shape: 55 bytes fill one block with the length, 56 need a second, 64 are a
// whole block before the padding, and 119 and 120 the same one block later.
// Computed 2026-10-14 with Python 3's hashlib.sha256(b"a" * n).hexdigest().
static_assert(hexText(sha256(std::string(55, 'a'))) ==
              "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
static_assert(hexText(sha256(std::string(56, 'a'))) ==
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
static_assert(hexText(sha256(std::string(63, 'a'))) ==
              "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
static_assert(hexText(sha256(std::string(64, 'a'))) ==
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
static_assert(hexText(sha256(std::string(119, 'a'))) ==
              "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
static_assert(hexText(sha256(std::string(120, 'a'))) ==
              "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c");

// --- the recording -------------------------------------------------------------

// Room for the files one program reads, and their paths. A whole suite run
// in one process read at most 33 files in Process Monitor's audit of
// 2026-10-12 (test_tonemap_port); a test that would exceed either limit fails
// by name rather than going unrecorded.
inline constexpr std::size_t kMaxFiles = 4096;
inline constexpr std::size_t kMaxPathBytes = 512;
inline constexpr std::size_t kMaxNameBytes = 512;

enum class ReadWhen : std::uint8_t { OutsideCases, InCase };

// What the running program has read. **Fixed in size, and so trivially
// destructible**: the texts must live as long as the program, since every
// view handed out points into them, and a static holding a std::string or a
// container would run a destructor at exit, which -Wexit-time-destructors
// reports. Arrays never move, so a view stays valid. A read before the first
// test case, or between two, belongs to every record written after it --
// whatever the program read is an input of each case it runs.
//
// **One thread.** Catch2 runs a program's cases one after another on one
// thread, and its own assertions are not safe from a second; no test reads
// from another thread, and one that did would need a lock here.
struct Recorder {
    std::array<std::array<char, kMaxPathBytes>, kMaxFiles> texts{};
    std::array<std::size_t, kMaxFiles> lengths{};
    std::array<ReadWhen, kMaxFiles> readWhen{};
    std::size_t count{};
    std::size_t caseStart{};
    std::array<char, kMaxNameBytes> name{};
    std::size_t nameLength{};
    ReadWhen phase{ReadWhen::OutsideCases};
};
static_assert(std::is_trivially_destructible_v<Recorder>);

[[nodiscard]] Recorder& recorder() noexcept {
    static constinit Recorder instance{};
    return instance;
}

[[nodiscard]] std::string_view textAt(const Recorder& r, std::size_t index) {
    return {r.texts.at(index).data(), r.lengths.at(index)};
}

// The two folders a test reads from, compiled in here and nowhere else
// (decision 530).
enum class Folder : std::uint8_t { Data, Probes };

[[nodiscard]] constexpr std::string_view folderText(Folder folder) noexcept {
    switch (folder) {
    case Folder::Data:
        return ORBSIM_DATA_DIR;
    case Folder::Probes:
        return ORBSIM_PROBE_DIR;
    }
    return {};
}

[[nodiscard]] std::string_view record(Folder where, std::string_view name) {
    Recorder& r = recorder();
    const std::string_view folder = folderText(where);
    const std::size_t length = folder.size() + 1 + name.size();
    if (r.count == kMaxFiles || length > kMaxPathBytes) {
        FAIL("tests/TestReads.cpp has no room to record " << folder << '/' << name
                                                          << ": raise kMaxFiles or kMaxPathBytes");
        return {};
    }
    std::array<char, kMaxPathBytes>& text = r.texts.at(r.count);
    const auto afterFolder = std::ranges::copy(folder, text.begin()).out;
    *afterFolder = '/';
    std::ranges::copy(name, std::next(afterFolder));
    r.lengths.at(r.count) = length;
    r.readWhen.at(r.count) = r.phase;
    ++r.count;
    return textAt(r, r.count - 1);
}

// A file's bytes, read in one call: a character at a time through a stream
// iterator took about a second for a 14 MiB probe frame in the Debug tree.
// The size is found by seeking, as VulkanContext::loadShaderModule does:
// std::filesystem::file_size is where clang-analyzer reports a false finding
// inside MSVC's own header (see test_file_replace.cpp).
[[nodiscard]] std::optional<std::vector<char>> charsOf(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    if (size < 0) return std::nullopt;
    in.seekg(0);
    std::vector<char> chars(static_cast<std::size_t>(size));
    in.read(chars.data(), size);
    if (in.gcount() != size) return std::nullopt;
    return chars;
}

[[nodiscard]] std::optional<std::vector<std::byte>> bytesOf(const std::filesystem::path& file) {
    const auto chars = charsOf(file);
    if (!chars) return std::nullopt;
    std::vector<std::byte> bytes(chars->size());
    std::ranges::transform(
        *chars, bytes.begin(), [](char c) noexcept { return static_cast<std::byte>(c); });
    return bytes;
}

// UTF-8 text as a path. Constructing a path from a char string reads it in
// the system's code page on Windows, not as UTF-8.
[[nodiscard]] std::filesystem::path pathOf(std::string_view utf8) {
    std::u8string text(utf8.size(), u8'\0');
    std::ranges::transform(
        utf8, text.begin(), [](char c) noexcept { return static_cast<char8_t>(c); });
    return std::filesystem::path{text};
}

// One line of a record: the hash of the file as it is now, or `missing`.
[[nodiscard]] std::string recordLine(std::string_view file) {
    const auto chars = charsOf(pathOf(file));
    std::string line;
    if (chars) {
        line += hexText(sha256(std::string_view{chars->data(), chars->size()}));
    } else {
        line += "missing";
    }
    line += ' ';
    line += file;
    line += '\n';
    return line;
}

// Whether `file` lies under the system's temporary directory, both made
// absolute and free of `.` and `..` first, component by component -- so that
// `C:/Temp2/x` is not under `C:/Temp`. A trailing separator on the directory
// is an empty last component.
[[nodiscard]] bool isUnderTemporaryDirectory(const std::filesystem::path& file) {
    std::error_code error;
    const std::filesystem::path scratch =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(error), error);
    if (error) return false;
    const std::filesystem::path inner = std::filesystem::weakly_canonical(file, error);
    if (error) return false;
    const auto [stop, ignored] = std::ranges::mismatch(scratch, inner);
    return stop == scratch.end() || (std::next(stop) == scratch.end() && stop->empty());
}

// Whether `file` is, character for character, a path dataFile() or
// probeFile() handed out in this program.
[[nodiscard]] bool wasRecorded(std::string_view file) {
    const Recorder& r = recorder();
    for (std::size_t i = 0; i < r.count; ++i) {
        if (textAt(r, i) == file) return true;
    }
    return false;
}

} // namespace

std::string_view dataFile(std::string_view relative) { return record(Folder::Data, relative); }

std::string_view probeFile(std::string_view name) { return record(Folder::Probes, name); }

std::expected<std::vector<std::byte>, FileReadError> readRecordedFile(std::string_view file) {
    if (!wasRecorded(file)) return std::unexpected(FileReadError::NotRecorded);
    auto bytes = bytesOf(pathOf(file));
    if (!bytes) return std::unexpected(FileReadError::CannotOpen);
    return *std::move(bytes);
}

std::expected<std::vector<std::byte>, FileReadError>
readScratchFile(const std::filesystem::path& file) {
    if (!isUnderTemporaryDirectory(file)) return std::unexpected(FileReadError::OutsideScratch);
    auto bytes = bytesOf(file);
    if (!bytes) return std::unexpected(FileReadError::CannotOpen);
    return *std::move(bytes);
}

void startReadRecord(std::string_view testName) {
    Recorder& r = recorder();
    r.phase = ReadWhen::InCase;
    r.caseStart = r.count;
    // A name too long to hold is kept as none: the case then writes no
    // record, and the driver runs it as one that does not record.
    r.nameLength = testName.size() <= kMaxNameBytes ? testName.size() : 0;
    std::ranges::copy(testName.substr(0, r.nameLength), r.name.begin());
}

void finishReadRecord() {
    Recorder& r = recorder();
    if (r.phase != ReadWhen::InCase) return;
    r.phase = ReadWhen::OutsideCases;
    if (r.nameLength == 0) {
        std::cerr << "orbsim tests: a test case's name is too long for its read record\n";
        return;
    }

    std::set<std::string_view> files;
    for (std::size_t i = 0; i < r.count; ++i) {
        if (i >= r.caseStart || r.readWhen.at(i) == ReadWhen::OutsideCases) {
            files.insert(textAt(r, i));
        }
    }
    std::string text;
    for (const std::string_view file : files) {
        text += recordLine(file);
    }
    // Last, so that a record cut off while it was written is not taken for
    // a whole one (scripts/run-tests.py requires it).
    text += "end\n";

    const std::string_view name{r.name.data(), r.nameLength};
    const std::filesystem::path folder = pathOf(ORBSIM_READ_RECORD_DIR);
    const std::filesystem::path recordFile = folder / pathOf(escapedName(name) + ".txt");
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    // Binary alone, as view/FileWrite.cpp opens: an output stream truncates
    // by itself, and openmode is a signed bitmask type.
    std::ofstream out{recordFile, std::ios::binary};
    out << text;
    out.close();
    // A record that could not be written is not an error of the test's: the
    // driver treats a test without one as not recording, and runs it whenever
    // anything it could read changes (decision 505). Said here, so that a
    // tree that never records is noticed rather than merely slow.
    if (!out) {
        std::cerr << "orbsim tests: the read record could not be written\n";
    }
}

} // namespace orb::test
