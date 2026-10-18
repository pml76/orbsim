//
// Tests for tests/WriterTurns.hpp: how the coverage recorder's writes of one
// record take turns (M1-131, register decisions 638 and 648).
//
// A write on another thread must wait while the turn is held, and get it once
// it is given back; a write on the thread that holds it -- a crash inside the
// write itself -- must give up at once; and a wait whose bound runs out must
// say so, for the recorder to end the program. The recorder's own self-test
// cannot show the first: its program has one thread, and a second write would
// have to arrive while the first is part-way, which no run of it can arrange
// every time. Here two threads are held in step, so that no verdict depends on
// how they happen to be scheduled: the second thread asks for the turn while
// the first holds it for as long as the second runs, or the first gives it
// back only once the second has started and records that it did.
//
#include "tests/TestCase.hpp"
#include "tests/WriterTurns.hpp"

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>

namespace {

using orb::coverage::giveTurnBack;
using orb::coverage::takeTurn;
using orb::coverage::Turn;

// A bound for the case that must see one run out, short so that the case is
// quick; nothing is decided by how short.
constexpr std::chrono::milliseconds kShortWait{20};
// A bound no case may reach: a minute, about a thousand times what the
// slowest case here takes.
constexpr std::chrono::milliseconds kNeverReached{60'000};

} // namespace

TEST_CASE("a free turn is taken at once and given back", "[writer-turns]") {
    std::atomic<std::thread::id> writer;
    REQUIRE(takeTurn(writer, kNeverReached) == Turn::Taken);
    CHECK(writer.load() == std::this_thread::get_id());
    giveTurnBack(writer);
    CHECK(writer.load() == std::thread::id{});
}

TEST_CASE("a writer on the thread holding the turn is told so at once", "[writer-turns]") {
    std::atomic<std::thread::id> writer;
    REQUIRE(takeTurn(writer, kNeverReached) == Turn::Taken);
    // The short bound: a writer that waited here instead would run out.
    CHECK(takeTurn(writer, kShortWait) == Turn::HeldByThisThread);
    CHECK(writer.load() == std::this_thread::get_id());
    giveTurnBack(writer);
}

TEST_CASE("a writer on another thread waits while the turn is held, and its bound running out is "
          "reported",
          "[writer-turns]") {
    std::atomic<std::thread::id> writer;
    REQUIRE(takeTurn(writer, kNeverReached) == Turn::Taken);
    Turn second = Turn::Taken;
    std::chrono::steady_clock::duration waited{};
    {
        // Joined at the end of the block: the turn is held for as long as
        // the second thread runs.
        const std::jthread other([&writer, &second, &waited] {
            const auto before = std::chrono::steady_clock::now();
            second = takeTurn(writer, kShortWait);
            waited = std::chrono::steady_clock::now() - before;
        });
    }
    CHECK(second == Turn::RanOut);
    CHECK(waited >= kShortWait);
    CHECK(writer.load() == std::this_thread::get_id());
    giveTurnBack(writer);
}

TEST_CASE("a writer on another thread gets the turn once it is given back", "[writer-turns]") {
    std::atomic<std::thread::id> writer;
    REQUIRE(takeTurn(writer, kNeverReached) == Turn::Taken);
    std::latch started{1};
    std::atomic<bool> givenBack{false};
    Turn second = Turn::RanOut;
    bool sawItGivenBack = false;
    bool heldIt = false;
    {
        const std::jthread other(
            [&writer, &started, &givenBack, &second, &sawItGivenBack, &heldIt] {
                started.count_down();
                second = takeTurn(writer, kNeverReached);
                sawItGivenBack = givenBack.load();
                heldIt = writer.load() == std::this_thread::get_id();
                giveTurnBack(writer);
            });
        started.wait();
        givenBack.store(true);
        giveTurnBack(writer);
    }
    CHECK(second == Turn::Taken);
    CHECK(sawItGivenBack);
    CHECK(heldIt);
    CHECK(writer.load() == std::thread::id{});
}
