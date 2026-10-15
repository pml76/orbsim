//
// Write each test case's read record as it ends (M1-129, register decisions
// 498 and 527): what tests/TestReads.cpp recorded while it ran, as
// `test-reads/<test name>.txt` in the build tree, which scripts/run-tests.py
// reads to decide whether the case must run again.
//
// **Compiled into every suite**, in the same OBJECT library as
// tests/AbortBehaviour.cpp and for the same reason: a listener in a static
// library is linked only into a program that already references that library,
// and a suite that reads no file would lose it silently -- then write no
// record, and be run on every change under data/ (decision 505) for no reason
// anybody could see. The CTest test `abort_listener` already requires that
// library in every suite, by asking each for the listener beside this one;
// and the driver requires a record from every Catch2 case that passed.
//
#include "tests/TestReads.hpp"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

class RecordWhatEachCaseRead final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        orb::test::startReadRecord(info.name);
    }

    void testCaseEnded(const Catch::TestCaseStats& /*stats*/) override {
        orb::test::finishReadRecord();
    }
};

} // namespace

CATCH_REGISTER_LISTENER(RecordWhatEachCaseRead)
