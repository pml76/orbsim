#ifndef ORBSIM_APP_SHADERRECORD_HPP
#define ORBSIM_APP_SHADERRECORD_HPP
//
// The application's record of the shader files a run asked the renderer for
// (M1-131, item 5; register decisions 629, 632, 637, 642 and 644).
//
// **Why.** The test driver fingerprints a test that starts the application
// on the shaders that test's runs loaded, rather than on every compiled
// shader, and learns which from this record (decision 629). Only when
// `ORBSIM_COVERAGE_RECORDS` names a folder, which the driver sets for its
// run: one file a process, `<process number>-<start time in ns>.shaders.txt`
// in that folder (decision 632), beside the coverage recorder's own. Without
// the variable nothing is written, and nothing else changes.
//
// **What it says** (decision 644), one item a line, in UTF-8:
//
//     test <ORBSIM_TEST_NAME, which may be empty>
//     <a shader file's full path, as the renderer was given it>
//     ...
//     end
//
// Each shader file once, however many pipelines load it, in the order first
// asked for; each line written and flushed as it happens, so that no way out
// of the program loses one already asked for -- a run stopped by a missing
// shader depended on it too (decision 629). **`end` is written by main()
// once run() has returned** (decision 637), so that every return completes
// the record, the error exits among them, and an abort, a crash or an
// escaping exception does not: a record without it counts as none, and a test
// that left none keeps every shader.
//
// **A record that cannot be written is not reported, and that is not coping
// silently** (ADR 0002). The record's one reader is told by the record itself:
// a folder that cannot be written in leaves no record, and a line that cannot
// be written -- a full disk, which a file stream meets without a word --
// leaves every later line unwritten, `end` among them, since a stream that
// has failed stays failed; either way the driver finds no complete record
// and keeps every shader for the test, the answer that was right before
// records existed. A message would change the output the tests compare, of a
// run whose own result does not depend on its record.
//
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace orb::app {

class ShaderRecord {
public:
    // The record ORBSIM_COVERAGE_RECORDS asks for, opened and its `test` line
    // written; where the variable is unset or empty, one that writes nothing.
    // Both variables are read through SDL_getenv (decision 644).
    [[nodiscard]] static ShaderRecord open();

    // A shader file the renderer was asked for, written the first time it is
    // asked for. Told apart by the path's text as given, which is one text a
    // file: every caller joins the one shader folder with the file's name.
    void writeShader(const std::filesystem::path& shader);

    // The last line, `end`.
    void writeEnd();

private:
    // Writes nothing.
    ShaderRecord() = default;
    // Opens `file` and writes the `test` line. open() returns either kind as
    // a prvalue, so that the record is built where it is kept and never
    // moved: a std::ofstream's move can throw under MSVC's library, which
    // bugprone-exception-escape rightly reports of a move constructor.
    explicit ShaderRecord(const std::filesystem::path& file);

    // One line, flushed at once.
    void writeLine(std::string_view line);

    std::ofstream file_;               // not open where no record was asked for
    std::vector<std::string> written_; // each shader line written, for once a file
};

} // namespace orb::app

#endif // ORBSIM_APP_SHADERRECORD_HPP
