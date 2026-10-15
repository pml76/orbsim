#ifndef ORBSIM_TESTS_TESTCASE_HPP
#define ORBSIM_TESTS_TESTCASE_HPP
//
// What every test file includes to write a test case, in place of Catch2's own
// catch2/catch_test_macros.hpp (M1-129, register decisions 550 and 573-577).
//
// Why it exists: `check` reruns a test only when a function it entered at its
// last run has changed in its compiled code (item 9, decision 543). Catch2, as
// it comes, defeats that in two ways, and this header undoes both while
// leaving Catch2's own files unchanged:
//
//   * **Catch2 compiles each check's line number into the code**, so an edit
//     changed every function below it. CATCH_INTERNAL_LINEINFO, which every
//     Catch2 macro expands, carries line 0 here instead (decision 550): a
//     failure is still reported by its file, test case, section and
//     expression, and no longer by its line.
//   * **Catch2 names each test's function by a running counter**,
//     `CATCH2_INTERNAL_TEST_<n>`, so inserting a test renamed every test after
//     it in the file, and in an optimised build every registration of a file
//     is merged into one start-up function that every test enters. Here a
//     test's function is a member of `Case<caseId("name", "[tags]")>`, so its
//     compiled name carries the test's own name and tags, and each test
//     registers in a start-up function of its own (decision 573). Measured on
//     2026-10-15 with a test inserted before two others: 7 functions changed
//     and 8 removed in Debug before, none of either after.
//
// What follows from it: a test's name is at most 127 characters and its tags
// at most 63, and a longer one stops the compile at testNameOrTagsTooLong();
// two tests with one name are a compile error rather than a start-up one; and
// a program runs its tests in another fixed order, which CTest, running one
// case at a time, does not see. tests/test_test_case.cpp proves the fixed line
// reaches Catch2's own macros (decision 574), and scripts/check-test-cases.py
// keeps every test file on this header (decision 575).
//
// It uses Catch2's own registration types -- AutoReg, makeTestInvoker,
// NameAndTags -- as Catch2's macros do. A Catch2 upgrade that changed them
// would stop the compile, not change a result.
//
// **A test case is held to clang-tidy's size and complexity limits like any
// other function** -- 60 statements, 80 lines, 5 levels of nesting and a
// cognitive complexity of 25 -- and Catch2's checks weigh heavily against them:
// a REQUIRE expands to a do-while around a try/catch, so it is several
// statements and about four points of complexity. A test case with more than
// about six checks therefore names groups of them as helper functions in its
// file, each held to the same limits (decisions 578 and 581). Until
// 2026-10-15 the size limits never reached a test body, Catch2's own TEST_CASE
// coming from a system header, and 69 test cases carried a suppression of the
// complexity limit instead, as ruled on 2026-09-09 (docs/HISTORY.md).

// The export mark makes clang-tidy's misc-include-cleaner take CHECK, REQUIRE
// and SECTION as provided by this header (decision 576).
#include <catch2/catch_test_macros.hpp> // IWYU pragma: export

#include "core/Attributes.hpp"

#include <catch2/internal/catch_source_line_info.hpp>
#include <catch2/internal/catch_stringref.hpp>
#include <catch2/internal/catch_test_registry.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

// Catch2 defines it in catch_source_line_info.hpp, included above, and its
// include guard keeps it from being defined again; every Catch2 macro expands
// it where it is used, so this definition is the one they see.
#undef CATCH_INTERNAL_LINEINFO
#define CATCH_INTERNAL_LINEINFO ::Catch::SourceLineInfo(__FILE__, 0)

namespace orbsim::test {

// Room for a test's name and tags, each with its terminating zero. The
// longest in the suites on 2026-10-15 were 87 and 28 characters.
inline constexpr std::size_t kCaseNameCapacity = 128;
inline constexpr std::size_t kCaseTagsCapacity = 64;

// A test case's name and tags as one value a template can be keyed by, which
// is what puts them into the compiled names.
struct CaseId {
    std::array<char, kCaseNameCapacity> name{};
    std::array<char, kCaseTagsCapacity> tags{};
};

// Catch2's tags, "[a][b]", as a type of their own, so that a call cannot swap
// them with the name.
class CaseTags {
public:
    // Not explicit: TEST_CASE("name", "[tag]") hands it a string literal.
    consteval explicit(false) CaseTags(const char* tags ORBSIM_LIFETIMEBOUND) : text_{tags} {}
    [[nodiscard]] consteval std::string_view text() const { return text_; }

private:
    std::string_view text_;
};

// Deliberately not constexpr, and never defined: a name or tags too long for
// CaseId reach it while caseId() is evaluated at compile time, and the compile
// stops, naming it.
void testNameOrTagsTooLong();

[[nodiscard]] consteval CaseId caseId(std::string_view name, CaseTags tags = "") {
    CaseId id{};
    if (name.size() >= id.name.size() || tags.text().size() >= id.tags.size()) {
        testNameOrTagsTooLong();
    }
    std::ranges::copy(name, id.name.begin());
    std::ranges::copy(tags.text(), id.tags.begin());
    return id;
}

// The file a test case is written in, for Catch2's reports; TEST_CASE gives
// each test its own value.
template <CaseId Id> inline constexpr const char* kCaseFile = nullptr;

// One test case: TEST_CASE defines run(), and the explicit instantiation it
// writes registers it at start-up through kRegistered. A static member of a
// class template is initialised on its own, not in the file's shared start-up
// function, which is what keeps one test's registration out of every other
// test's record.
template <CaseId Id> struct Case {
    static void run();

    // noexcept: Catch2's AutoReg reports a failure to register later rather
    // than throwing, and an allocation failure here ends the program, as it
    // would in Catch2's own registration.
    [[nodiscard]] static bool registerCase() noexcept {
        const Catch::AutoReg registration{
            Catch::makeTestInvoker(&Case::run),
            ::Catch::SourceLineInfo(kCaseFile<Id>, 0),
            Catch::StringRef(),
            Catch::NameAndTags{Catch::StringRef(Id.name.data()), Catch::StringRef(Id.tags.data())}};
        return true;
    }

    static const bool kRegistered;
};

// A test registers itself when the program starts: that is what Catch2 is, and
// Catch2's own macros switch the same warning off where they register
// (CATCH_INTERNAL_SUPPRESS_GLOBALS_WARNINGS). Off at this one definition alone,
// by the owner's go-ahead (decision 573, Non-negotiable 10).
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
template <CaseId Id> const bool Case<Id>::kRegistered = Case<Id>::registerCase();
#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace orbsim::test

// Catch2's TEST_CASE, replaced. The test's body follows the macro, as with
// Catch2's: it becomes the definition of Case<...>::run(). Before it, the
// test's file is given to kCaseFile, run() is declared so the instantiation
// leaves it alone, and the explicit instantiation registers the test.
#undef TEST_CASE
// A macro, with the owner's go-ahead (decision 573): a test case is a name and
// a body that follows it, which only a macro can take, and Catch2's own
// TEST_CASE is one too.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define TEST_CASE(...)                                                                             \
    template <>                                                                                    \
    inline constexpr const char* ::orbsim::test::kCaseFile<::orbsim::test::caseId(__VA_ARGS__)> =  \
        __FILE__;                                                                                  \
    template <> void ::orbsim::test::Case<::orbsim::test::caseId(__VA_ARGS__)>::run();             \
    template struct ::orbsim::test::Case<::orbsim::test::caseId(__VA_ARGS__)>;                     \
    template <> void ::orbsim::test::Case<::orbsim::test::caseId(__VA_ARGS__)>::run()

#endif // ORBSIM_TESTS_TESTCASE_HPP
