#ifndef ORBSIM_VIEW_RENDERQUALITY_HPP
#define ORBSIM_VIEW_RENDERQUALITY_HPP
//
// What the renderer is allowed to trade for frame time (M1-12, ADR 0007).
//
// **Empty of fields on purpose, and that is the task rather than a stub.**
// Threading a settings value through a renderer that was built for one fixed
// configuration is the same class of retrofit as the HDR pipeline itself, and
// it is nearly free while there is one draw call and nothing drawn. The fields
// arrive with the features that cost frames: the atmosphere's lookup tables
// and its scattering order in M1-46, the quadtree's threshold, level cap and
// cache budget in M1-59.
//
// **The simulation cannot see this, and the link graph is what says so.**
// orbsim_core links neither orbsim_view nor orbsim_render, so a physics
// translation unit that reaches for a quality setting does not compile -- and
// a configure with -DORBSIM_BUILD_APP=OFF builds the core and every physics
// suite without this header existing at all. That is ADR 0007's load-bearing
// clause, and ADR 0012 is why the struct lives here rather than in
// src/render/: the quadtree, the atmosphere and this value are all headless,
// so they can be tested together without a graphics device.
//
// **Why it must never reach the physics**, in one sentence, because the
// pressure to break this always arrives disguised as convenience: the moment a
// dropped frame can change a trajectory, a replay stops reproducing and the
// difference between a model error and a code defect collapses. Physics cost
// is answered by decoupling the clocks, never by consulting the renderer.
//
// **The shape the fields will take** (ADR 0007 has the argument; this is what
// a reader of this file needs): one field per thing that can independently
// cost frames; an `enum class` for a discrete choice and a strong type
// carrying its unit for a continuous one, never an int and never a bool; and a
// default member initialiser on every field, so that adding one next year
// cannot leave an older construction site reading rubbish.
//
// **There is deliberately no operator==.** The first fields to arrive include
// a Pixels, and comparing two doubles with == is what CODING_GUIDELINES
// section 11 forbids and -Wfloat-equal reports. Nothing needs to ask whether
// two quality values are the same; the adaptive controller in a later
// milestone produces new ones rather than comparing old ones.
//
#include "core/Scalar.hpp" // for constantEvaluable, used by the assertions below

#include <concepts>
#include <type_traits>

namespace orb::view {

// A plain aggregate: Rule of Zero, trivially copyable, cheap to pass by value
// and to snapshot once per frame.
struct RenderQuality {
    // The named presets are constexpr factories rather than a stored tier
    // (ADR 0007). Most people want one control and they still get one -- the
    // preset *constructs* the value instead of being the representation of it,
    // so "high atmosphere, low terrain" stays expressible on hardware that
    // wants it, which this machine is: a discrete graphics card beside a
    // laptop processor.
    //
    // **All four return the same value today, and that is expected.** There is
    // nothing yet for them to differ in; M1-46 gives the struct its first
    // fields and the presets their first disagreement, and each will then
    // carry a comment saying what machine it is for rather than only what it
    // contains.
    //
    // Presets cannot fail, so they return a value rather than a
    // std::expected. A quality value read from a configuration file can fail,
    // and `fromConfig` will report it by name -- that is deferred with
    // scenario serialisation, because it is the same question.
    [[nodiscard]] static constexpr RenderQuality low() noexcept { return RenderQuality{}; }
    [[nodiscard]] static constexpr RenderQuality medium() noexcept { return RenderQuality{}; }
    [[nodiscard]] static constexpr RenderQuality high() noexcept { return RenderQuality{}; }
    [[nodiscard]] static constexpr RenderQuality ultra() noexcept { return RenderQuality{}; }
};

// The properties ADR 0007 depends on, checked rather than claimed.
static_assert(std::is_aggregate_v<RenderQuality>, "a plain aggregate, per ADR 0007");
static_assert(std::is_trivially_copyable_v<RenderQuality>,
              "so that a per-frame snapshot costs nothing worth measuring");
static_assert(std::is_trivially_default_constructible_v<RenderQuality>,
              "Rule of Zero: nothing here is hand-written, so nothing here can be forgotten");
static_assert(std::is_nothrow_default_constructible_v<RenderQuality>);
static_assert(std::is_empty_v<RenderQuality>,
              "no fields yet -- which stops being true at M1-46, and should");
static_assert(!std::equality_comparable<RenderQuality>,
              "two quality values are deliberately not comparable; see the note above");

// **Every preset, worked out while the compiler runs.** Written as an
// assertion rather than as four constexpr variables, which was the first
// attempt: a constexpr variable that cannot be initialised is an ordinary
// build error, and an ordinary build error is indistinguishable from a
// mistake in the mutant when the mutation harness meets one. An assertion
// that fires says which claim broke.
//
// constantEvaluable comes from core/Scalar.hpp, where Count's wraparound
// guard uses it for the same purpose.
static_assert(orb::constantEvaluable<decltype([] { return RenderQuality::low(); })> &&
                  orb::constantEvaluable<decltype([] { return RenderQuality::medium(); })> &&
                  orb::constantEvaluable<decltype([] { return RenderQuality::high(); })> &&
                  orb::constantEvaluable<decltype([] { return RenderQuality::ultra(); })>,
              "every preset is usable in a constant expression, which is what makes it a preset "
              "rather than a stored tier");

} // namespace orb::view

#endif // ORBSIM_VIEW_RENDERQUALITY_HPP
