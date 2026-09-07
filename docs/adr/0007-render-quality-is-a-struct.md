# ADR 0007: Render quality is a struct of per-feature settings, and never reaches the simulation

Status: accepted (2026-09-07)

## Decision

Visual fidelity is adjustable; physical fidelity is not
([`0006`](0006-simulation-not-sandbox.md)). The mechanism is a value type in
the renderer:

- **`RenderQuality` is a struct of per-feature settings**, one field per thing
  that can independently cost frames. It is a plain aggregate -- Rule of Zero,
  trivially copyable, cheap to pass by value and to snapshot per frame.
- **Discrete choices are `enum class`; continuous quantities are strong types
  carrying their unit.** Not `int` and not `bool`, per non-negotiables 1 and 2.
  `quality.shadows = 2` is a mystery at the assignment and
  `quality.cascades = 4096` transposed with a resolution compiles silently.
- **Named presets are `constexpr` factories**, not a stored tier:
  `RenderQuality::low()`, `::medium()`, `::high()`, `::ultra()`. Most people
  want one control, and they still get one -- the preset constructs the struct
  rather than being the representation of it.
- **Settings that arrive from a config file are validated and reported**,
  through `fromConfig(...) -> std::expected<RenderQuality, RenderError>`
  (ADR 0002). The presets cannot fail and carry a `static_assert`; user input
  can and is reported by name.
- **`RenderQuality` lives in `src/render/` and the simulation cannot see it.**
  This is the load-bearing clause. It is enforced by the link graph rather than
  by discipline: `orbsim_core` does not link `orbsim_render`, so a physics
  translation unit that tries to read a quality setting does not compile. That
  is guideline section 12 doing the work, and it is why this record exists at
  all.
- **The adaptive controller is separate from the value.** A frame-time
  controller produces new `RenderQuality` values; it is policy, and it does not
  belong inside the data. It may read the frame clock. **The physics may not.**
  It needs hysteresis, or it oscillates at the threshold and the oscillation is
  more visible than the quality difference it is managing.

Illustrative shape -- the field list grows with the features, and it is the
shape rather than the contents that this record fixes:

```cpp
// src/render/RenderQuality.hpp
enum class CloudDetail : std::uint8_t { Off, Flat, Layered, Volumetric };
enum class ShadowDetail : std::uint8_t { Off, Hard, Soft };
enum class OceanDetail : std::uint8_t { Diffuse, Specular, GlintBrdf };
enum class ScatteringOrder : std::uint8_t { Single, Multiple };
enum class Antialiasing : std::uint8_t { None, Fxaa, Taa };

struct RenderQuality {
    CloudDetail clouds{CloudDetail::Layered};
    ShadowDetail shadows{ShadowDetail::Soft};
    OceanDetail ocean{OceanDetail::Specular};
    ScatteringOrder scattering{ScatteringOrder::Multiple};
    Antialiasing antialiasing{Antialiasing::Taa};

    Texels skyViewLut{192};       // sky-view LUT edge length
    Texels shadowMap{2048};
    Pixels screenSpaceError{2.0}; // quadtree subdivision threshold
    Mebibytes tileCache{512};
    Ratio renderScale{1.0};       // dynamic resolution

    [[nodiscard]] static constexpr RenderQuality high() noexcept;
    [[nodiscard]] static std::expected<RenderQuality, RenderError>
    fromConfig(const ConfigTable& table);
};
```

Enumerators are declared cheapest-first so the declaration reads as a ramp.
That ordering is a reading convenience and **not** part of the interface:
nothing should compare two enumerators with `<`, because "cheaper" is not
transitive across hardware.

Every field carries a default member initializer, so a `RenderQuality` cannot
exist uninitialised and adding a field next year cannot leave an old
construction site reading garbage (guideline section 6).

**The determinism claim gets a test**, which is
[`../VERIFICATION.md`](../VERIFICATION.md) rule 16 applied to quality rather
than to time: run one scenario at two presets and assert the simulation state
is bit-identical. It is cheap, it can be written long before the quality system
is finished, and it is what catches a rendering concern leaking downward.

## What we considered

**A single global tier, `enum class Quality { Low, Medium, High, Ultra }`.**
The simplest thing, one control in a menu, and what most software ships. It was
rejected because this very machine is the counterexample: an RTX A2000 beside a
laptop CPU. Terrain LOD is CPU- and IO-bound -- quadtree traversal, tile
streaming, cache eviction -- while atmospheric scattering is almost pure GPU.
One tier cannot say "high atmosphere, low terrain", which is exactly the
setting this hardware wants. A single tier also forces every feature added
later into four buckets that were fixed before the feature existed.

The presets survive that rejection intact. What changes is that the tier
becomes a *constructor* rather than the representation, which costs one
function and keeps the simple control.

**A string-keyed dictionary of settings**, the cvar system most engines grow.
Maximally flexible, defers every error to runtime, and type-checks nothing.
That is the opposite of every other decision in this codebase.

**Plain `int` and `bool` fields in a settings struct.** What most projects do,
and it fails at the assignment site rather than at the call site, which is
worse: `quality.clouds = 3` cannot be read, and two adjacent `int` resolutions
transpose in silence. Non-negotiable 1 and I.24 apply to struct fields for the
same reason they apply to parameters.

**Templating the renderer on the quality settings**, resolving every branch at
compile time. Genuinely zero-cost, and it makes runtime adjustment impossible
-- which kills the adaptive controller outright -- while multiplying build time
by the number of instantiated combinations. Build time is a feature
(guideline section 17).

**Putting `RenderQuality` in `core/` so the simulation can read it.** Tempting
for one specific reason: if the frame rate drops, the physics could do less
work. **This is the coupling the whole design exists to prevent.** The moment a
dropped frame can change a trajectory, a replay stops reproducing, a bug stops
being reproducible, and the distinction between a model error and a code bug --
ADR 0006's entire argument -- collapses. Physics cost is answered by decoupling
the clocks, never by consulting the renderer.

## Why

Fluency is part of realism: a stuttering view from orbit does not read as real
however correct the scattering is, so frame time is part of the image and
deserves a mechanism rather than a hope.

But the two halves of the simulator need *opposite* treatment, and the reason
to write this down is that the difference is easy to lose. The visuals are a
perception problem, so approximating them is a legitimate trade the user should
control. The physics is a truth problem, so approximating it is not a trade at
all -- it produces a different simulation whose results cannot be compared
against the error budgets ADR 0006 makes mandatory.

A settings system that cannot tell those apart will eventually be asked to help
with a frame-rate problem by touching the physics, and it will look reasonable
at the time. Making it a compile error is cheaper than making it a rule.

The struct-over-tier half is a smaller argument and a more ordinary one: a
single tier silently couples subsystems that have no reason to be coupled, and
the coupling only shows up on hardware the author did not have.

## What this record does not decide

- **The field list.** It grows as features land, starting empty in phase A.
- **The numeric base for count-like units.** `Quantity<Derived>` in
  `core/Scalar.hpp` holds an `f64`, and `Texels` or `Mebibytes` want an
  integral base. That is either a small generalisation of `Quantity` or a
  second base beside it, and it should be decided when the first such field is
  actually written rather than guessed at now.
- **Whether the adaptive controller ships in milestone 1**, or whether quality
  stays manual until there is something worth adapting.
- **The default preset at first run**, and whether hardware detection picks it.
- **The config file format**, which is the same question as scenario
  serialisation and should be answered once, for both.