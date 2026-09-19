#include "core/LeapSeconds.hpp"

#include "core/Contract.hpp"

#include <cstdint>
#include <optional>
#include <span>

//
// The reverse direction: what a TAI instant needs to know about the table.
//
// Why this half is out of line while the UTC-day half is constexpr in the
// header is written on the declaration in core/LeapSeconds.hpp. In one line:
// core/Time.hpp evaluates the UTC-day queries while building kUnixEpoch, and a
// constant expression cannot call a function defined in another translation
// unit.
//
// There is no second table here. UTC day `utcMjd` begins `deltaAtSeconds` into
// the same TAI day, so each row is already its own TAI key -- the same 28 rows,
// read with a different comparison. A mirror array would be a second
// transcription of the same facts, and the whole point of ADR 0009's table is
// that there is one.
//

namespace orb {

namespace {

// The step in force at `instant`, and the one after it if there is one.
//
// A range-for over the rows rather than over indices: a subscript is what
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access objects to, and
// an iteration that cannot leave the table is a better answer than a checked
// subscript. The rows are sixteen bytes and are returned by value, so no
// lifetime question follows the span out of this function.
struct StepsAround {
    std::optional<LeapSecondStep> inForce;
    std::optional<LeapSecondStep> next;
};

[[nodiscard]] StepsAround stepsAroundTai(std::span<const LeapSecondStep> table,
                                         TaiDayAndSecond instant) noexcept {
    StepsAround found;
    for (const LeapSecondStep& step : table) {
        if (taiInstantOf(step) > instant) {
            found.next = step;
            break;
        }
        found.inForce = step;
    }
    return found;
}

} // namespace

TaiLookup leapSecondLookupForTai(std::span<const LeapSecondStep> table,
                                 TaiDayAndSecond instant) noexcept {
    ORBSIM_EXPECTS(!table.empty());
    if (table.empty()) return {};

    const StepsAround around = stepsAroundTai(table, instant);
    ORBSIM_EXPECTS(around.inForce.has_value());
    // As on deltaAtSecondsForUtcDay: reachable only from a Release build whose
    // caller skipped the era check, and present so that what follows has a
    // value to work with rather than to answer the question.
    if (!around.inForce) return {.deltaAtSeconds = table.front().deltaAtSeconds};

    const std::int32_t deltaAt = around.inForce->deltaAtSeconds;
    if (!around.next) return {.deltaAtSeconds = deltaAt};

    const LeapSecondStep& next = *around.next;
    const std::int32_t inserted = next.deltaAtSeconds - deltaAt;
    ORBSIM_EXPECTS(inserted >= -1 && inserted <= 1);
    if (inserted <= 0) {
        // A negative leap second removes 23:59:59 rather than adding a label,
        // so there is no window to be inside: the next step takes over exactly
        // where the removed second would have begun, and the naive subtraction
        // in core/Time.hpp skips it on its own.
        return {.deltaAtSeconds = deltaAt};
    }

    // The inserted second occupies [nextKey - inserted, nextKey) in TAI. The
    // subtraction cannot borrow a day: DeltaAT is at least 10 s at every row,
    // and the table is asserted to keep it inside a day.
    const TaiDayAndSecond insertedBegins{
        .mjd = next.utcMjd,
        .secondOfDay = next.deltaAtSeconds - inserted,
    };
    ORBSIM_ENSURES(insertedBegins.secondOfDay >= 0);
    return {
        .deltaAtSeconds = deltaAt,
        // `instant < nextKey` already holds: index is the *last* key at or
        // before it, so only the lower end needs testing.
        .insideInsertedLeapSecond = instant >= insertedBegins,
    };
}

TaiLookup leapSecondLookupForTai(TaiDayAndSecond instant) noexcept {
    return leapSecondLookupForTai(kIersLeapSeconds, instant);
}

} // namespace orb
