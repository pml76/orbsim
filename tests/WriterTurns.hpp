#ifndef ORBSIM_TESTS_WRITERTURNS_HPP
#define ORBSIM_TESTS_WRITERTURNS_HPP
//
// How the coverage recorder's writes of one record take turns (M1-131,
// register decisions 638 and 648).
//
// tests/CoverageRecorder.cpp writes a program's record when it ends normally,
// and from handlers when it stops: on an abort, a crash and a bad argument to
// the C runtime. The crash handler acts before any code that may catch the
// exception, so it can write in a program that then goes on, and two writes
// can meet. They take turns (decision 638):
//
//   * a write on another thread **waits** until the first has finished, and
//     only then reads the flags and writes, so that the last record written
//     is the fullest -- one that gave up instead would leave the earlier
//     record, whole in form and missing every function entered since;
//   * a write on the thread already writing -- a crash inside the write
//     itself -- **gives up at once**, and the record that write cut off lacks
//     its `end` line, so it counts as none (decision 637);
//   * the wait is **bounded** (Non-negotiable 4): where the bound runs out,
//     something is already stuck, and the caller is told, so that the recorder
//     ends the program as failed.
//
// A header of its own, with no Windows in it, so that tests/test_writer_turns.cpp
// can hold two threads in step and prove each of the three (decision 648): the
// recorder's own self-test has one thread, and cannot.
//
#include <atomic>
#include <chrono>
#include <thread>

namespace orb::coverage {

// How long a write waits for another thread's to finish before deciding that
// something is stuck (decision 638): "well above the longest write", which
// M1-131's item 4 measures and which then sets it. 10 s until that
// measurement -- a placeholder, not a measured figure. The recorder passes it;
// tests/test_writer_turns.cpp passes bounds of its own.
inline constexpr std::chrono::seconds kLongestWait{10};
// How often a waiting write looks again whether its turn has come.
inline constexpr std::chrono::milliseconds kLookAgainAfter{1};

enum class Turn : unsigned char {
    Taken,            // this thread writes now, and gives the turn back after
    HeldByThisThread, // this thread is writing already: give up
    RanOut,           // another thread held it past the bound: something is stuck
};

// Takes the turn `writer` holds, as above. `writer` names the thread writing
// now, or none.
[[nodiscard]] inline Turn takeTurn(std::atomic<std::thread::id>& writer,
                                   std::chrono::milliseconds longestWait) {
    const std::thread::id self = std::this_thread::get_id();
    const auto giveUpAt = std::chrono::steady_clock::now() + longestWait;
    std::thread::id holder;
    while (!writer.compare_exchange_strong(holder, self)) {
        if (holder == self) return Turn::HeldByThisThread;
        if (std::chrono::steady_clock::now() >= giveUpAt) return Turn::RanOut;
        std::this_thread::sleep_for(kLookAgainAfter);
        holder = std::thread::id{};
    }
    return Turn::Taken;
}

inline void giveTurnBack(std::atomic<std::thread::id>& writer) { writer.store(std::thread::id{}); }

} // namespace orb::coverage

#endif // ORBSIM_TESTS_WRITERTURNS_HPP
