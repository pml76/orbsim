//
// The out-of-line half of tests/OrbitTestSupport.hpp: each matcher's
// describe(), which is its key function -- the first virtual defined out of
// line -- so that the matcher's vtable is emitted here, once, rather than in
// every suite that includes the header. When every virtual was inline, each
// of the four translation units that included it emitted its own copy, which
// is what -Wweak-vtables reports on the Itanium ABI (ADR 0017).
//
// The three definitions are exempt from gcc's -Wabi-tag, for the reason given
// on WithinAbsOf::describe() in the header: the std::string is Catch2's.
//
#include "tests/OrbitTestSupport.hpp"

#include <format>
#include <string>

namespace orb::test {

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif

std::string WithinAbsOf::describe() const {
    return std::format("is within {:g} of {:.17g}", tol_.value, want_);
}

std::string WithinRelTo::describe() const {
    return std::format("is within {:g} relative of {:.17g}", relTol_.value, want_);
}

std::string WithinRelVec::describe() const {
    return std::format("is within {:g} relative of ({:.17g}, {:.17g}, {:.17g})",
                       relTol_.value,
                       want_.x,
                       want_.y,
                       want_.z);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace orb::test
