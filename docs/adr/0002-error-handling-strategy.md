# ADR 0002: `std::expected` for expected failures, assertions for impossible ones

Status: accepted (2026-09-05)

## Decision

One strategy, applied uniformly, with one error representation per layer:

- **Conditions a caller can legitimately produce** are reported by returning
  `std::expected<T, Error>`. A NaN in a scenario file, an eccentricity out of
  range, a lost GPU, a malformed command-line argument -- these are normal
  control flow, and every function that can meet one says so in its return
  type and is `[[nodiscard]]`.
- **Conditions that cannot happen unless the code is wrong** are asserted
  with `ORBSIM_EXPECTS` / `ORBSIM_ENSURES` (`core/Contract.hpp`). That a
  wrapped angle lies in `(-pi, pi]` is not a runtime possibility; it is a
  claim about the function above it. Assertions never contain side effects,
  because they vanish under `NDEBUG`.
- **The representation fits the layer.** The orbital core reports an
  `OrbitError` enum, because its failures are the handful this code can
  decide and each has a `describe()`. The renderer reports a `RenderError`
  carrying a string, because its failures are reported *by the driver* and
  the driver's own text (`VK_ERROR_DEVICE_LOST`, the missing feature's name)
  is the useful part. The app layer reports `SdlError`, likewise a message.
  One way of signalling failure, not one representation of it everywhere.
- **Every `VkResult` is checked**, through one helper that names the result.
  `[[nodiscard]]` cannot reach a C API; the helper is how rule 7 of the Power
  of Ten (check every return value) is kept there.
- **Nothing throws deliberately, and `main` catches anyway.** An exception
  escaping `main` is `std::terminate` with no message; `std::print` can throw
  when stdout is closed, and the standard library can when memory runs out.
  The handlers use `std::fputs`, because a reporting path that can itself
  throw is not a reporting path.
- **Failures are refused by name, first.** `propagate()` checks its inputs for
  NaN and infinity before any arithmetic and reports `NotFinite`, because a
  NaN that reaches Newton comes out as "did not converge" -- true, and
  misleading, since it sends the reader to the solver instead of the file.

## What we considered

**Exceptions throughout.** *C++ Best Practices* has a section headed "Use
Exceptions", and the Core Guidelines say the same in E.2 and NR.3. The
argument is strong: an exception cannot be silently ignored the way a return
code can. We landed elsewhere because `std::expected` did not exist when most
of that advice was written, and `[[nodiscard]]` closes precisely the gap the
argument is about -- ignoring the result is now a compile-time diagnostic.

**`bool` plus an out-parameter**, which the renderer had. It leaves the caller
asking what the out-parameter holds on success, and it forced two-phase
construction: an object existed, then `init()` decided whether it was usable.
E.5 and NR.5 both argue against that, and `VulkanContext::create` is a factory
for exactly this reason.

**A single error type for everything.** Tempting for uniformity, wrong for the
core: an enum can be switched on and tested for identity, and the tests do
both. A string cannot.

**Reporting "did not converge" for NaN.** What the code did before `NotFinite`
existed. Honest in the narrowest sense and unhelpful in every other.

## Why

The failure the guidelines warn about is not "you chose wrong". It is "you
never chose", and a codebase where a third of the functions throw, a third
return `bool` and a third return `std::expected` is worse than any one of
those done uniformly.

The line between "report" and "assert" is the useful part. An eccentricity of
1.5 in a scenario file is a Tuesday; a mean anomaly outside `[-pi, pi]`
immediately after `wrapPi` returned it is a bug in `wrapPi`. Reporting the
first lets the caller explain it to a user. Asserting the second makes it fail
loudly, in the debugger, at the moment it happens -- rather than twenty
minutes later as a spacecraft in the wrong place. Which is also why the
definition of done runs the tests in a Debug tree: an assertion that never
executes protects nothing.