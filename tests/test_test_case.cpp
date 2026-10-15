//
// Tests for tests/TestCase.hpp: that the line it fixes reaches Catch2's own
// macros, and that a test case keyed by its name reaches Catch2 as written
// (M1-129, register decisions 550, 573 and 574).
//
// Why a test and not a static_assert: the header defines
// CATCH_INTERNAL_LINEINFO itself, so a check of that definition would still
// pass if a Catch2 upgrade renamed the macro its own macros expand, while the
// line numbers came back into every function. What these read is what Catch2
// recorded. Seen failing on 2026-10-15 without the header, naming the real
// lines.
//
#include "tests/TestCase.hpp"

#include <catch2/catch_assertion_result.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/interfaces/catch_interfaces_capture.hpp>
#include <catch2/interfaces/catch_interfaces_registry_hub.hpp>
#include <catch2/interfaces/catch_interfaces_testcase.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace {

// The registered test case of this name, or none.
[[nodiscard]] const Catch::TestCaseInfo* registeredCase(const std::string& name) {
    const std::vector<Catch::TestCaseHandle>& all =
        Catch::getRegistryHub().getTestCaseRegistry().getAllTests();
    const auto found = std::ranges::find_if(all, [&name](const Catch::TestCaseHandle& handle) {
        return handle.getTestCaseInfo().name == name;
    });
    return found == all.end() ? nullptr : &found->getTestCaseInfo();
}

} // namespace

TEST_CASE("an assertion is recorded without its line", "[test-case]") {
    // A failure that does not fail the test: Catch2 keeps the result of a
    // non-passing assertion, and not of a passing one, whose result it does
    // not build at all.
    CHECK_NOFAIL(false);
    const Catch::AssertionResult* const last = Catch::getResultCapture().getLastResult();
    REQUIRE(last != nullptr);
    CHECK(last->getSourceInfo().line == 0);
}

TEST_CASE("every test case is registered without its line", "[test-case]") {
    const std::vector<Catch::TestCaseHandle>& all =
        Catch::getRegistryHub().getTestCaseRegistry().getAllTests();
    REQUIRE_FALSE(all.empty());
    for (const Catch::TestCaseHandle& handle : all) {
        INFO(handle.getTestCaseInfo().name);
        CHECK(handle.getTestCaseInfo().lineInfo.line == 0);
    }
}

TEST_CASE("a test case's name and tags reach Catch2 as written", "[test-case][second-tag]") {
    const Catch::TestCaseInfo* const info =
        registeredCase("a test case's name and tags reach Catch2 as written");
    REQUIRE(info != nullptr);
    std::vector<std::string> tags;
    std::ranges::transform(info->tags, std::back_inserter(tags), [](const Catch::Tag& tag) {
        return std::string{tag.original};
    });
    // Catch2 keeps the tags in an order of its own, so the order is not the claim.
    std::ranges::sort(tags);
    CHECK(tags == std::vector<std::string>{"second-tag", "test-case"});
    // The file a failure is reported in is this one, not the header that
    // registered the test.
    CHECK(std::string{info->lineInfo.file}.ends_with("test_test_case.cpp"));
}
