# M1-12 — `Count<Derived>` and `RenderQuality`

Phase: A | Status: not started
Prerequisites: M1-09

## Purpose

ADR 0007 decided the shape of render quality and deliberately left two things
open: the numeric base for count-like units, and when the struct arrives. Both
are settled now — an integral sibling of `Quantity`, and here, empty.

**The point is the path, not the knobs.** Threading a settings value through a
renderer built for one fixed configuration is the same class of retrofit as the
HDR pipeline itself, and it is nearly free while there is one draw call.

## What to implement

- **`core/Scalar.hpp` gains `Count<Derived>`**, the integral sibling of
  `Quantity<Derived>`: one `std::uint32_t`, explicit construction, no implicit
  conversion either way, comparison, addition and subtraction of the same type,
  and multiplication by an unsigned scale. No division that could silently
  truncate — if a ratio of counts is wanted, it is a named function returning
  `f64`. The same CRTP shape and the same `friend Derived` trick, so
  `struct Other : Count<Texels>` does not compile.
- **`core/Units.hpp` gains `Pixels`** on the existing f64 `Quantity`, because a
  screen-space error threshold of 2.5 px is a real quantity rather than a
  count.
- **`src/view/RenderQuality.hpp`**: the struct, empty of fields for now, with
  `constexpr` factories `low()`, `medium()`, `high()` and `ultra()` that all
  currently return the same empty value — and a comment saying that is expected,
  because the fields arrive with the features (M1-46 and M1-59).
- **`Texels` and `Mebibytes`** as `Count` types, defined now so the first
  feature to need them adds a field rather than a type system.
- **The path**: the application owns one `RenderQuality`, passes it into the
  frame, and the renderer takes it by value per frame. Nothing reads it yet.
  A frame that cannot see the quality value is a frame that will be rewritten
  when it needs to.

## Out of scope

Any actual setting. `fromConfig` and any configuration file — deferred with
scenario serialisation. The adaptive controller. Reading a quality value
anywhere in the physics, which is impossible by construction and is the point of
ADR 0007.

## Tests

Extends `tests/test_view_math.cpp` or a new `tests/test_render_quality.cpp`.

- **Compile-time proofs**: `sizeof(Texels) == sizeof(std::uint32_t)`; trivially
  copyable; `!std::is_convertible_v<std::uint32_t, Texels>` and the reverse;
  `Texels` does not convert to `Mebibytes`; the presets are usable in a constant
  expression.
- **Arithmetic**: addition and subtraction stay in the type; a `Count` cannot be
  divided into another `Count` implicitly; unsigned wraparound at zero is
  reported by a precondition rather than silently producing four billion.
- **`RenderQuality` is an aggregate** with all members default-initialised, and
  copying it is trivial — asserted, not claimed, because ADR 0007 depends on
  cheap per-frame snapshots.

## The structural check

`orbsim_core` must not link `orbsim_view`. That is what makes "a physics
translation unit that reads a quality setting does not compile" true rather than
hoped for. The check is the `-DORBSIM_BUILD_APP=OFF` build from M1-09, which
builds the core and every physics suite without `orbsim_view` existing at all.

## Done when

- [ ] `check` green in both trees.
- [ ] ADR 0007's open question is closed, and ADR 0001 carries the dated note
      about `Count` (both written in M1-02, updated here if the shape changed).
- [ ] A `RenderQuality` value reaches the frame code, and nothing reads it.
- [ ] `-DORBSIM_BUILD_APP=OFF` still builds core and its tests.
