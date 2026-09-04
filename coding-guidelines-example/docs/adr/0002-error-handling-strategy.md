# ADR 0002: `std::expected` for expected failures, assertions for impossible ones

Status: accepted

## Decision

One strategy, applied uniformly:

- **Conditions a caller can legitimately produce** are reported by returning
  `std::expected<T, SomeError>`. Eccentricity out of range, a non-positive
  gravitational parameter, a request for two samples -- these arrive from
  scenario files and user input, and they are normal control flow.
- **Conditions that cannot happen unless the code is wrong** are asserted with
  `ORBEX_EXPECTS` / `ORBEX_ENSURES`. That a wrapped angle lies in `[-pi, pi]` is
  not a runtime possibility; it is a claim about the function above it.
- **Nothing in the simulation throws deliberately.** `main` still catches,
  because an exception escaping `main` is `std::terminate` with no message, and
  `std::print` can throw if stdout is closed.

Every error enum has a `describe()` returning a `std::string_view`, so no
failure can reach a user as a bare number.

## What we considered

**Exceptions throughout.** *C++ Best Practices* has a section headed simply "Use
Exceptions", and the Core Guidelines say the same in E.2 and NR.3. The argument
is strong: an exception cannot be silently ignored the way a return code can.
The reason we landed elsewhere is that `std::expected` did not exist when most
of that advice was written, and `[[nodiscard]]` closes precisely the gap the
argument is about -- ignoring the result is now a compile-time diagnostic.

**`bool` plus an out-parameter**, which is what the parent project's
`VulkanContext` currently does. It works, but it leaves the caller asking what
the out-parameter holds on success, and it forces two-phase construction: an
object exists, then `init()` decides whether it is usable. That is NR.5 and E.5
both, and `OrbitPath::sample` avoids it by being a factory.

**Error codes with a global "last error".** Rejected without much thought: it is
not thread-safe, and section 13 assumes this code runs threaded eventually.

## Why

The failure the guidelines actually warn about is not "you chose wrong" -- it is
"you never chose", and a codebase where a third of the functions throw, a third
return `bool` and a third return `std::expected` is worse than any one of those
done uniformly.

The line between "report" and "assert" is the useful part of this decision. An
eccentricity of 1.5 in a scenario file is a Tuesday; a mean anomaly outside
`[-pi, pi]` immediately after `wrapToPi` returned it is a bug in `wrapToPi`.
Reporting the first lets the caller explain it to a user. Asserting the second
makes it fail loudly, in the debugger, at the moment it happens -- rather than
twenty minutes later as a spacecraft in the wrong place.

Assertions never contain side effects. They vanish under `NDEBUG`, and a side
effect that vanishes with them is a bug that exists only in the configuration
you ship. Where the result is needed, it is computed into a variable first and
the variable is asserted.
