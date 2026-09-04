# orbsim C++ Guidelines

*Written in the style of Jason Turner (C++ Weekly, "C++ Best Practices") as a
stylistic homage. He did not write this and has never seen this codebase.*

---

Hi, I'm Jason Turner, and today we're going to talk about the code in *your*
project.

Here's the thing about a space flight simulator: you have picked a problem
domain where the computer will absolutely, positively tell you when you are
wrong. Your spacecraft will fly into the ground. Your orbit will spiral into
the planet because you lost four digits of precision somewhere. There is no
hiding. I love this, because it means we get *feedback*, and feedback is how
we get better.

So let's talk about how to write this code so the feedback comes early, from
the compiler, instead of late, from a vehicle that just deorbited itself.

None of this replaces the **C++ Core Guidelines**. Where a rule below has a
number behind it — I.4, E.5, CP.50 — that number is Stroustrup and Sutter's, not
mine, and [Appendix B](#appendix-b-cross-reference-with-the-c-core-guidelines)
maps every section here back to theirs. Read this document for the opinions and
the arguments about *your* code. Read theirs for the completeness.

---

## If you read nothing else

This document got long, which is a maintainability problem in its own right.
So here is the whole thing on one screen. Everything below is elaboration.

1. **Turn on every warning, then `-Werror`.** The compiler is a free reviewer.
2. **Put units in the type system.** `Radians` and `Degrees` are structs, not
   `double`. The Mars Climate Orbiter is the cautionary tale, and it is in your
   exact problem domain.
3. **No boolean parameters.** If the call site needs a `/*hostVisible=*/`
   comment, the parameter wanted to be an `enum class`.
4. **Rule of Zero.** Every hand-written destructor is a bug you volunteered for.
   Wrap the resource once; the class that holds it then needs nothing.
5. **`const` by default, `[[nodiscard]]` on anything whose return value is the
   point.**
6. **Pick one error strategy and keep it.** `std::expected` is available to you.
7. **Bound every loop, and say so when it does not converge.**
8. **Measure before you optimize, and measure before you believe anyone who
   says something is slow.**
9. **`f64` in the simulation, `f32` only at the GPU boundary**, and never touch
   the floating-point flags.
10. **The physics must not know the renderer exists.**
11. **Commit in small, coherent steps, and say why in the message.** The repo
    exists now; `git bisect` is what will one day find the commit that broke a
    trajectory.

---

## 1. Use the tools available

This is always where I start, and it is always the section people skip. Don't
skip it. Every hour you spend on tooling pays you back for the life of the
project.

The full inventory is in [the Toolbox appendix](#appendix-a-the-toolbox) at the
end. Here are the ones I want to actually talk about.

**Turn the warnings on. All of them. Then turn them into errors.**

```cmake
-Wall -Wextra -Wpedantic
-Wshadow                  # a shadowed variable in an integrator is a very bad day
-Wnon-virtual-dtor
-Wold-style-cast          # you are writing C++23, not C
-Wcast-align
-Wunused
-Woverloaded-virtual
-Wconversion              # see below, this one matters enormously here
-Wsign-conversion
-Wnull-dereference
-Wdouble-promotion
-Wformat=2
-Wimplicit-fallthrough
```

The compiler is the cheapest code reviewer you will ever have. It works for
free, it never gets tired, and it never gets tired of telling you the same
thing. Let it.

**Done.** Every target now links an `orbsim_warnings` INTERFACE target
carrying the list above plus `-Werror`, so no target can quietly opt out.
Dependencies are pulled in with `SYSTEM`, which is what makes that survivable:
the full set appeared to produce 643 warnings, and 639 of them were inside
`vk_mem_alloc.h` and `VkBootstrap.h`.

**A note on `-Wconversion` in this project specifically.** You are going to get
a *lot* of hits, because this codebase deliberately computes in `double` and
hands `float` to the GPU. That is not a reason to turn the warning off. That is
the warning doing exactly its job: every single narrowing is a place where you
are throwing away precision, and each one should be a visible, deliberate
`static_cast<float>` that a reader can find. Make the precision boundary
something you can *grep for*.

**Sanitizers.** On this toolchain (clang targeting the MSVC ABI), AddressSanitizer
works and you should have a build configured with it:

```cmake
-fsanitize=address -fno-omit-frame-pointer
```

UndefinedBehaviorSanitizer has only partial support on Windows, so don't promise
yourself more than you're getting. If this ever grows a Linux CI job, turn on
`-fsanitize=address,undefined` there and let it find things the Windows build
cannot.

**Static analysis.** `clang-tidy` is right there. You already have a
`compile_commands.json` being generated, which is the only hard part. Start with
`bugprone-*`, `performance-*`, and `readability-*`, and add
`cppcoreguidelines-*` when you are feeling strong.

**Tests.** You already have 546 assertions on the two-body core, and — this is
the part I want to highlight — they check two *independent* implementations
against each other. Universal-variable propagation versus Kepler-element
propagation. Neither one can hide a sign error behind the other. That is a
genuinely good test design and you should keep doing exactly that as the physics
grows. A test that only checks the code against itself tells you nothing.

---

## 2. Express intent, and make interfaces hard to misuse

The Core Guidelines open with philosophy - P.1 "Express ideas directly in code",
P.3 "Express intent", P.4 "Ideally, a program should be statically type safe",
P.5 "Prefer compile-time checking to run-time checking". Those four rules are
one idea wearing four hats: **push your meaning into the type system, where the
compiler can check it, instead of leaving it in your head, where it cannot.**

For an orbital mechanics simulator, this is not philosophy. This is the whole
ballgame. So let's talk about I.4, "Make interfaces precisely and strongly
typed", which I think is the single highest-value rule in the entire document
for *this* codebase.

### Units are types. Yours were not.

This is what `core/Math.hpp` used to offer:

```cpp
inline constexpr f64 rad(f64 degrees);
inline constexpr f64 deg(f64 radians);
```

Both took an `f64`. Both returned an `f64`. Nothing - *nothing* - stopped you
writing `rad(rad(x))`, or handing degrees to a function that expected radians.
The compiler would smile and produce a spacecraft that flew somewhere
interesting.

**Fixed.** `core/Units.hpp` now defines `Radians`, `Degrees`, `Metres`,
`Seconds`, `Eccentricity` and `GravParam`, and `Elements` is built from them.
The rest of this section is why.

In 1999 the Mars Climate Orbiter was destroyed because one team's software
produced impulse in pound-force seconds and another team's expected
newton-seconds. Nobody was careless. The types simply did not carry the
information, so nothing could catch it. That mission cost about $327 million and
it is the most expensive unit-conversion bug in history.

You are writing software in the same problem domain. Take the hint.

A strong type is a struct and a constructor. That is the entire trick:

```cpp
struct Radians {
    f64 value{};
    explicit constexpr Radians(f64 v) : value(v) {}
};

struct Degrees {
    f64 value{};
    explicit constexpr Degrees(f64 v) : value(v) {}
};

constexpr Radians toRadians(Degrees d) { return Radians{d.value * (kPi / 180.0)}; }

constexpr Radians operator""_rad(long double v) { return Radians{static_cast<f64>(v)}; }
constexpr Degrees operator""_deg(long double v) { return Degrees{static_cast<f64>(v)}; }
```

Now `51.6_deg` is a *thing*, the conversion is explicit and named, and passing
the wrong one is a compile error instead of a mission. It costs you a couple of
dozen lines in a header, it is entirely `constexpr`, and at `-O2` it compiles to
exactly the same machine code as the bare `double`. Go check that on Compiler
Explorer, because you should not believe me - you should believe the
disassembly.

The same argument applies to metres versus kilometres, and to seconds versus
days once you have time acceleration running.

Two footnotes on strong types:

- **Mark single-argument constructors `explicit`**, as `Radians` above does.
  Leave it off and the type converts back to a bare `f64` on its own, which
  rebuilds the exact problem you were solving.
- **You do not have to hand-roll this.** `strong_type`, `NamedType` and
  `type_safe` are small header-only libraries that generate the boilerplate
  including the arithmetic operators. Two hand-written types is fine. Eight is
  a library.

### I.24: adjacent parameters you can swap

Rule I.24 is "Avoid adjacent parameters that can be invoked by the same
arguments in either order". Here is what the propagator used to be:

```cpp
StateVector propagate(const StateVector& sv, f64 mu, f64 dt);

propagate(state, dt, mu);   // compiled perfectly. flew you into the sun.
```

Two adjacent `f64` parameters, silently swappable. The test helpers had three
of them in a row - `checkNear(what, got, want, tol)`. Strong types fixed this
for free, as a side effect of fixing the units problem. That is what a good
abstraction does: you buy one thing and get another.

Today it reads `propagate(const StateVector&, GravParam, Seconds)`, the helper
takes a `Tolerance`, and `bugprone-easily-swappable-parameters` is enabled to
keep it that way. That check is not decoration: it is what found the
`nearlyEqual(f64, f64, f64)` defect in the worked example.

Related: I.23, "Keep the number of function arguments low". If you find yourself
passing five doubles, you have discovered a struct that wants to exist.

### Boolean parameters are a mystery at the call site

This one is from Jason's own collection rather than the Core Guidelines. The
renderer had three instances of it, now all fixed - `Validation`, `Memory` and
`FenceState` - but the argument is worth keeping, because the next one will
look just as harmless:

```cpp
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
VkFence makeFence(VkDevice device, bool signalled);
bool init(SDL_Window* window, bool enableValidation, std::string& error);
```

Read this at the call site and tell me what it does:

```cpp
createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);   // true... what?
```

Now here is the interesting part. The code did not actually look like that.
It looked like this:

```cpp
createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, /*hostVisible=*/true);
makeFence(device_, /*signalled=*/true);
makeFence(device_, /*signalled=*/false);
```

Every single boolean call site had a hand-written comment explaining the
argument. **That comment is the evidence.** Whoever wrote it already knew the
type was not carrying its meaning, and patched it with a comment the compiler
cannot check and nobody has to keep accurate.

The type system will do this for you, for free, and check it:

```cpp
enum class Memory { DeviceLocal, HostVisible };
enum class FenceState { Unsignalled, Signalled };

createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Memory::HostVisible);
makeFence(device_, FenceState::Signalled);
```

Now it reads, you cannot pass the wrong one, and when a third memory kind shows
up — and it will, the moment you want `HostVisibleCached` for readback — you
extend an enum instead of adding a second `bool` and creating the notorious
`f(true, false)` call site.

Anywhere you find yourself writing `/*name=*/` at a call site, the parameter is
telling you what it wanted to be.

### I.5, I.6, I.7: say what you require

State your preconditions. `mu` must be positive. `ecc` must not be negative. A
radius must be non-zero.

`propagate()` used to open with:

```cpp
if (r0 <= 0.0) return sv;
```

A zero radius is not a *situation*, it is a *bug* - a spacecraft at the exact
centre of a planet. Silently returning the input turned a loud, findable error
into a spacecraft that mysteriously stopped moving three hours into a flight,
for reasons nobody could reconstruct.

Decide which it is. If it is a precondition, assert it and say so. If it is a
supported input, document what the function guarantees. What you must not do is
leave it ambiguous, because ambiguous is how it stays broken.

**Fixed:** it now returns `std::unexpected(OrbitError::DegenerateState)`, and
there is a test that says so. The split between reporting and asserting lives in
`core/Contract.hpp`.

Until C++26 contracts land, an assert macro and a comment are enough. The
Guidelines call these `Expects()` and `Ensures()`; the names matter less than
the habit.

And one trap, since this section is now telling you to write assertions:
**never put anything with a side effect inside `assert()`.** Assertions vanish
in release builds and take the side effect with them, which gives you a bug
that exists only in the configuration you ship.

```cpp
assert(solveKepler(meanAnomaly, ecc));   // gone in release. never solved.

[[maybe_unused]] const auto ok = solveKepler(meanAnomaly, ecc);
assert(ok);                              // correct
```

---

## 3. Make it `constexpr`

You knew this was coming.

Every function you write, ask: *could this run at compile time?* Not "will it",
*could it*. If the answer is yes, mark it `constexpr` and let the caller decide.

```cpp
constexpr Vec3 cross(const Vec3& a, const Vec3& b) { ... }   // yes, good
```

And then — this is the part people forget — **prove it**:

```cpp
static_assert(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 0, 1});
```

That `static_assert` is a unit test that costs zero runtime, can never rot, and
runs on every single build whether you remember to run the test suite or not.
`Math.hpp` had `dot`, `cross`, `lengthSq` and every operator `constexpr`
already, with nothing checking them; there are `static_assert`s under them now,
and more under the unit conversions in `Units.hpp`.

In C++23 you can go further than you could a few years ago — `std::sqrt` is not
`constexpr`, but a great deal of your element-manipulation code is *nearly*
there. Push on it. Compile-time is the cheapest time.

---

## 4. Rule of Zero

Write classes that need no destructor, no copy constructor, no assignment
operator, and no move operations. The best special member function is the one
you did not have to write, because you cannot get it wrong.

Which brings me to what used to be the uncomfortable part.

**`VulkanContext` was a Rule of Five class pretending to be a Rule of Zero
class.** It held nineteen raw handles. `shutdown()` destroyed them in a
carefully hand-written order, and `~VulkanContext()` called it.

Look at what that cost: every single early `return false` inside `init()` left
the object partially constructed, so correctness depended on `shutdown()`
checking each handle against `VK_NULL_HANDLE` — forever, including for the
handles added next month. One day somebody adds a handle and edits only one of
the two places.

The fix is boring and it works: wrap each handle in a small move-only RAII type,
delete `shutdown()` entirely, and let the compiler generate the destructor.
Destruction order becomes reverse declaration order, which is a rule the
*language* enforces instead of a rule you enforce.

**Done.** `render/VulkanHandle.hpp` now contains every hand-written destructor
in the renderer, and `VulkanContext` declares none at all.

And it is worth knowing what that refactor turned up. Deleting `shutdown()` also
deleted the `vkDeviceWaitIdle` it opened with, so teardown began destroying
command pools and a swapchain the GPU was still using. The validation layers
said so on the first run. The fix was not to remember the wait — it was to make
it a member, `DeviceIdleGuard`, declared last and therefore destroyed first, so
that nobody editing the member list can drop it again.

That is the argument for RAII in miniature: the version that relies on a person
remembering fails quietly, and the version that encodes the requirement in a
type cannot.

---

## 5. `const` and `[[nodiscard]]`

Make everything `const` that can be `const`. Local variables. Member functions.
References. Yes, all of them. `const` is not decoration — it is you telling the
next reader "you do not need to track this one, it never changes", and the next
reader is you in five months.

Your orbital code was always good about `const`. `[[nodiscard]]` was the gap,
and it is closed: every orbital entry point and every renderer accessor has it
now.

`[[nodiscard]]` goes on any function whose entire purpose is its return value:

```cpp
[[nodiscard]] Elements elementsFromState(const StateVector& sv, f64 mu);
[[nodiscard]] StateVector propagate(const StateVector& sv, f64 mu, f64 dt);
```

Calling `propagate()` and dropping the result is *always* a bug. Ignoring the
`bool` from `uploadBuffer()` is *always* a bug. Make the compiler say so. This
costs you one attribute and buys you a whole category of mistake you can never
make again.

One counterpart, because it is the standard overcorrection: **do not pass or
return simple types by `const&`.** `const f64& mu` is slower than `f64 mu`, not
faster — a reference is an indirection the compiler has to chase, where a
`double` travels in a register. Pass primitives by value. `const&` starts
earning its keep at the first type big enough that copying it costs more than
the indirection, and your code already gets this right.

---

## 6. Initialize your variables

```cpp
f64 x{}, y{}, z{};        // yes
f64 x, y, z;              // no
```

`Vec3` already does this. Do it everywhere else too. Uninitialized reads are
undefined behavior, and undefined behavior in a physics integrator does not
politely crash — it produces a number, and the number is wrong, and it is wrong
in a way that looks *almost* plausible for about ninety seconds of simulated
flight.

Prefer `{}` braced initialization generally. It won't silently narrow on you,
which in a codebase that mixes `f64` and `f32` is worth real money.

Three more from the same instinct:

- **Use the member initializer list, not assignment in the constructor body.**
  Assigning means the member was default-constructed first and then
  overwritten. For a `std::vector` that is an allocation you paid for and threw
  away.
- **Write the list in declaration order.** Members initialize in the order they
  are *declared*, not the order you list them, so a mismatched list is how a
  member gets initialized from another member that does not have a value yet.
  `-Wreorder` catches it, and it is already in `-Wall`.
- **Default member initializers cover every constructor at once** — including
  the one you add next year and forget to update. `Vec3`'s `f64 x{}, y{}, z{};`
  is doing this, which is why `Vec3` cannot be constructed uninitialized.

---

## 7. Error handling

The Core Guidelines devote a whole section to this (E.1 through E.8) and open
with E.1: **"Develop an error-handling strategy early in a design."** Early.
Not once it hurts. You are at exactly the right moment to do this deliberately,
because the codebase currently has two strategies and has not chosen between
them.

### What you had

Both layers have now chosen `std::expected`. This is what they replaced:

```cpp
bool init(SDL_Window* window, bool enableValidation, std::string& error);
bool uploadBuffer(Buffer& dst, const void* data, VkDeviceSize size, std::string& error);
```

Boolean return, error text through an out-parameter. The Guidelines would push
you toward E.2, "Throw an exception to signal that a function cannot perform its
assigned task", and NR.3 is blunt: **"Don't avoid exceptions."** People avoid
them out of folklore - "exceptions are slow" - and folklore is not measurement
(see Per.6, and see the Toolbox).

But I am not going to tell you to throw here, because there is a real argument
on the other side: a missing GPU feature is an *expected outcome* that you
intend to explain to the user, not an exceptional one. E.3 says use exceptions
for error handling only, and reasonable people put device enumeration on the
normal-control-flow side of that line.

### What C++23 gives you instead

You are on C++23. You do not have to choose between exceptions and out-params,
because `std::expected` exists — and this is now the actual signature:

```cpp
[[nodiscard]] static std::expected<VulkanContext, InitError> create(SDL_Window*, Validation);
```

This is strictly better than what you have:

- The error cannot be silently dropped, because `[[nodiscard]]`.
- There is no half-constructed object to reason about.
- The out-parameter disappears, and with it the question of what `error` holds
  on success.
- It composes: `and_then`, `or_else`, `transform`.

### And it fixes a Rule of Zero problem too

E.5 says **"Let a constructor establish an invariant, and throw if it cannot."**
NR.5 is the same idea from the other side: do not use two-phase initialization.

`VulkanContext` was two-phase - construct, then `init()`, and between those two
lines it was a live object holding nothing. The factory collapses that: either
you have a fully valid context, or you have an error. There is no third state to
write defensive code against, which means there is no third state to *forget* to
write defensive code against.

That was the same argument as section 4, arriving from a different direction.
When two independent principles point at the same refactor, that is usually the
refactor to do — and in this case doing it once satisfied both.

One deliberate difference between the two layers: the core reports an
`OrbitError` enum, the renderer an `InitError` carrying a string. That is not
inconsistency. A degenerate orbit is one of three things this code can decide;
a device-creation failure is the *driver's* to explain, and "no suitable GPU"
tells a user less than the driver's own account of which feature was missing.
One strategy per layer means one way of signalling failure, not one
representation of it.

### The rest

- **E.6, use RAII to prevent leaks.** Already covered in section 4. It is the
  same rule. The Guidelines repeat it across three sections because it matters
  that much.
- **E.8, prefer `noexcept`.** Put it on move operations and on the small value
  types - `Vec3`, `Quat`, and their operators. It enables optimizations and,
  more importantly, it documents a promise.
- **E.4, design your error handling around invariants.** For the physics: decide
  now what `propagate()` promises when handed a degenerate orbit. Write it down.
  The answer "it depends what the caller does" is how you get two callers doing
  different things.
- **Whatever you choose, be consistent.** One strategy per layer. A codebase
  where a third of the functions throw, a third return `bool`, and a third
  return `std::expected` is worse than any one of those three done uniformly.

One honest note, because I do not want to quietly contradict myself. *C++ Best
Practices* has a section headed simply **"Use Exceptions"**, and it is right
about the reason: an exception cannot be silently ignored the way a return code
can. The reason this section lands somewhere else is that `std::expected` did
not exist when most of that advice was written, and `[[nodiscard]]` closes
precisely the gap the argument is about.

So if you prefer exceptions, use them — deliberately, and everywhere. The
failure this section is warning about is not "you chose wrong". It is "you
never chose".

---

## 8. Things we simply do not do

Short list. No debate.

- **No `using namespace std;`.** Not at file scope, not in headers, not ever.
- **No `std::endl`.** It flushes. You did not ask it to flush. Use `'\n'`.
- **No C-style casts.** `static_cast`, and if you find yourself reaching for
  `reinterpret_cast`, stop and be certain. (The one in `loadShaderModule` for
  the SPIR-V read is legitimate. Most are not.)
- **No raw `new` / `delete`.** Prefer values. Then containers. Then
  `unique_ptr`. `shared_ptr` is a last resort that means "I have not decided who
  owns this."
- **No owning raw pointers**, including in structs. A raw pointer means
  "observer, does not own, outlives me." Nothing else.
- **No `#define` for constants.** `constexpr` has a type and obeys scope. The
  preprocessor has neither.
- **Prefer `enum class`** over plain `enum`. Scoped, typed, no surprise
  conversions.
- **`std::array`, never a C array.** It knows its own size and you get `.at()`
  when you want it.

---

## 9. Prefer algorithms over raw loops

Sean Parent said "no raw loops" and he was right. If you are writing
`for (size_t i = 0; i < v.size(); ++i)`, ask what you are actually doing —
transforming, accumulating, finding, partitioning? — and say *that* instead.
The named algorithm cannot have an off-by-one error, because it does not have a
`+ 1` in it anywhere.

You have C++23 and ranges. `std::views::transform`, `std::views::filter`,
`std::ranges::sort`. When you start sampling orbit paths into vertex buffers,
that is a `transform`, not a loop.

The exception is code where you have measured and the loop wins. Measured. Not
guessed. Which brings us to:

---

## 10. Measure. Do not guess.

You will be tempted to hand-optimize the integrator. Resist until you have a
number.

Put a profiler on it. Look at the disassembly on Compiler Explorer. Write a
benchmark. Your intuition about what is slow is wrong — mine is too, and I have
been doing this a long time. The first time you profile this simulator you will
discover that the thing eating your frame is not the orbital mechanics at all;
it is something dull like a per-frame allocation you forgot about.

And the corollary: **do not pessimize**. There is a difference between premature
optimization and gratuitously doing the slow thing. Pass `std::string_view` for
non-owning string parameters. Pass sink parameters by value and `std::move`
them. Reserve your vectors when you know the size. These are not optimizations,
they are just... not being wasteful, and they cost nothing to get right the
first time.

---

## 11. Floating point, because this project actually cares

Most projects can be sloppy here. Yours cannot.

- **`-ffp-contract=off` is already set on `orbsim_core`, and that is correct.**
  FMA contraction reorders your arithmetic, and reordered arithmetic breaks the
  round-trip identities the tests depend on. Do not "clean up" that flag.
- **Never turn on `-ffast-math`.** Not on the core, not on anything that touches
  it. It permits the compiler to assume no NaNs and no infinities, and your
  `orbitInfo()` returns infinity on purpose, for hyperbolic trajectories.
- **Never compare floats with `==`.** You know this. I am saying it anyway,
  because someone will do it in a debug check at 1am.
- **Keep the precision boundary in one place.** `f64` everywhere in the
  simulation; narrow to `f32` only at the renderer boundary, after the
  camera-relative transform has brought the magnitudes down. The moment `f32`
  leaks upstream into the physics, you get jitter that is nearly impossible to
  trace back to its source.

---

## 12. Keep the layers apart

`orbsim_core` does not know that Vulkan exists. It does not know that SDL
exists. It builds and runs headless.

Protect this. It is worth more than it looks:

- The physics is testable without a GPU, which is why you have 546 assertions
  and not six.
- A scenario batch-runner, a dedicated server, or a headless CI job all become
  possible for free.
- The renderer can be replaced without touching a line of orbital mechanics.

The pressure to violate this always arrives disguised as convenience — "I just
need the vessel to know its screen position." It doesn't. Push the dependency
the other way, every time.

---

## 13. Concurrency, before you need it

You are not threaded yet. Read this anyway, because CP.1 is **"Assume that your
code will run as part of a multi-threaded system"**, and the cheapest time to
get this right is while there is still only one thread.

A simulator will thread eventually. The physics wants a fixed timestep; the
renderer wants to run as fast as the display allows. Those are different clocks,
and sooner or later you will pull them apart.

The design that works, and that follows CP.3, "Minimize sharing of mutable
data":

- The physics thread owns the world state. Nobody else touches it.
- Each tick, it publishes an immutable **snapshot**.
- The render thread reads the two most recent snapshots and interpolates.
- No locks in the hot path, because there is nothing shared to lock.

That is CP.4, "Think in terms of tasks, rather than threads", applied to a game
loop. You are not sharing memory and synchronizing access to it; you are handing
over finished values.

The rules to keep in your pocket:

- **CP.50: use `std::jthread`**, never a raw `std::thread`. It joins in its
  destructor and carries a `stop_token`. A `std::thread` you forget to join
  calls `std::terminate`, which is a rude way to find out.
- **CP.2 / CP.40: look for data races.** ThreadSanitizer, the day you thread
  anything. This is not optional and it is not a nice-to-have; a data race is
  undefined behavior, and here it will present as a physics bug.
- **CP.20: mutexes or atomics for shared data.** Never `volatile` - CP.51 exists
  because people keep believing this and it has never been true.
- **CP.31: pass small amounts of data between threads by value.** Copying a
  `StateVector` is six doubles. Do not get clever.
- **CP.44: name your lock guards.** An unnamed `std::lock_guard{m}` is a
  temporary that dies at the end of the full expression, which means it locks
  nothing at all. This bug is invisible in review, and TSan finds it instantly.

And the meta-rule: **do not thread this yet.** Per.6 - do not make claims about
performance without measurements. Thread it when a profiler tells you to, not
when your intuition does.

---

## 14. Source files

The dullest section in the Core Guidelines and one of the most quietly valuable,
because header hygiene decides whether your build takes six seconds or six
minutes two years from now.

- **SF.11: header files should be self-contained.** Every header must compile on
  its own, without the caller having remembered to include something first. The
  test is mechanical: for every `Foo.hpp`, compile a translation unit containing
  nothing but `#include "Foo.hpp"`. Put it in CI and it stays true forever.
- **SF.5: a `.cpp` file must include the header that specifies its interface**,
  and include it *first*. `Orbit.cpp` includes `Orbit.hpp` on line 1, which
  means `Orbit.hpp` is continuously proving SF.11 for free. That is not an
  accident of style, that is the mechanism. Keep doing it.
- **SF.7: never `using namespace` at global scope in a header.** You do not.
  Good. The moment one appears, every file downstream inherits it and you cannot
  take it back.
- **SF.9: avoid cyclic dependencies.** The `core` -> `orbit` -> `sim` ->
  `render` direction is currently clean and one-way. Section 12 protects that at
  the architectural level; this is the same rule at the file level.
- **Unnamed namespaces for internal linkage.** `Orbit.cpp` and
  `VulkanContext.cpp` both wrap their helpers in an anonymous namespace. That is
  exactly right - it keeps them out of the linker's sight and lets you rename
  them without rebuilding the world.
- **SF.1 asks for `.h`; you use `.hpp`.** That is fine. The rule is about
  consistency, and you are consistent. Do not churn the tree over this.

`include-what-you-use` automates most of the above. See the Toolbox.

---

## 15. Naming and formatting

I care much less about this than about everything above, and so should you. But
consistency has real value, so:

- Types: `PascalCase`. Functions and variables: `camelCase`. Private members:
  `trailingUnderscore_`. Constants: `kPascalCase`.
- Namespaces short and lowercase: `orb`, `orb::gfx`.
- Write a `.clang-format` and stop discussing it. Any style, checked in, applied
  automatically, is better than the best style applied by hand.

- **Never start an identifier with an underscore.** `_foo` at namespace scope,
  and anything containing `__`, is reserved for the implementation. That is not
  a style preference — it is undefined behaviour lying in wait for a standard
  library update. A *trailing* underscore (`device_`) is the safe form, and the
  one this codebase already uses.
- **Mark single-argument constructors `explicit`** so your types stop
  converting themselves behind your back.

The one naming rule I *will* argue about: **name things what they are, not what
type they are.** `meanToEccentricAnomaly` is a good name. It tells you the
domain concept. Nobody has ever been helped by `doubleConverter2`.

### A documented deviation

*C++ Best Practices* says braces are required around every block. This codebase
uses single-line guard clauses — `if (r0 <= 0.0) return sv;` — and `.clang-tidy`
has `readability-braces-around-statements` disabled to match.

That is a deliberate choice, written down here, and applied consistently, which
is the actual requirement. The thing you must never have is half the codebase
each way and an argument in every review. If you would rather have the braces,
change it once, flip the check on, and let the tool keep it that way.

---

## 16. Comments

Comment the **why**, never the **what**. The code already says what it does; if
it doesn't, fix the code instead of apologizing for it in a comment.

This codebase is currently good at this. Look at the comment explaining why
whole revolutions get folded out before the Newton iteration, or why reverse-Z
depth is used. Those are comments that save a future reader an hour of
confusion, because the *reason* is not recoverable from the code. Keep writing
those. Delete anything that just narrates the next line.

And when you make a non-obvious numerical choice — a tolerance, an iteration
count, a starting guess — say where it came from. `1e-9` with no explanation is
a mystery. `1e-9` with a sentence about why is engineering.

---

## 17. Maintainability: the boring things that decide it

Everything above is about the code. This section is about everything around the
code, which is what actually determines whether this project is pleasant to work
on in two years or a thing you dread opening.

### Version control

This one is done: the project is now a git repository with an initial commit of
all 21 source files, a `.gitignore` that keeps `build/`, `cmake-build-debug/`
and `.idea/` out of history, and a `.gitattributes` that normalises line endings
to LF so git and `.editorconfig` stop disagreeing.

It is worth saying why this was the first thing fixed, because it looks like
bookkeeping and it is not. You are building a physics simulator. One day a
trajectory will be subtly wrong and you will know it was right two weeks ago.
With history that is `git bisect` — a binary search over commits, culprit in
fifteen minutes. Without history it is archaeology. **Everything else in this
document makes bugs less likely. Version control is what makes them findable.**

Keeping it worth having:

- **Commit in small, coherent steps.** A commit that does one thing can be
  reverted. A commit that does nine cannot.
- **Say why in the message, not what.** The diff already says what.
- **Never commit generated output.** It makes diffs unreadable and the
  repository enormous.
- **The formatting pass gets its own commit**, doing nothing else. A reformat
  mixed with a logic change is a diff nobody can review, so nobody does.

### The config files, and what each one buys

These now exist, and each one converts a section of this document from advice
into something a machine enforces:

| File | What it mechanises |
|---|---|
| `.clang-format` | Section 15. Tuned to the code that already exists, so adopting it is low-churn |
| `.clang-tidy` | Much of sections 2, 5, 8 and 17, including a function-size limit |
| `.editorconfig` | Indentation and encoding, for editors that never run clang-format |
| `.gitattributes` | LF normalisation, so a line-ending flip never hides a real diff |
| `.gitignore` | Keeps build output out of history |

Two things worth knowing about how they are set up:

- **`.clang-format` has not been run over the tree yet.** It would touch roughly
  500 lines across seven files, almost all of it expanding multi-statement
  one-liners. That belongs in its own commit, per the rule above.
- **`.clang-tidy` deliberately enables `bugprone-easily-swappable-parameters`.**
  That is the mechanical enforcement of I.24 from section 2, and it *will* fire
  on `propagate(sv, mu, dt)`. That is not a false positive, that is the finding.
  Every disabled check in that file has a written reason beside it, because a
  suppression list without reasons is how a lint config quietly stops meaning
  anything.

### Functions should fit on a screen

`VulkanContext::init()` was **149 lines**. It created an instance, a surface,
picked a device, created a device, built an allocator, allocated command pools,
built sync objects, and created an upload context. That is eight jobs.

The Core Guidelines say it in F.2 and F.3 — a function should do one thing, and
be short. NASA says roughly sixty lines (section 20). Neither number is magic;
what matters is that a 149-line function cannot be understood without scrolling,
cannot be tested in pieces, and gives every one of its eight failure paths the
same undifferentiated `return false`.

**Done.** It is 29 lines now, delegating to `makeInstanceAndSurface`,
`makeDevice`, `createAllocator`, `createFrameResources` and
`createUploadContext`. Each fits on a screen and reports its own specific
failure. The first two are file-local rather than members, which is what keeps
`vkb::Instance` out of the header - see "Build time is a feature" below.
`createSwapchain` and `beginFrame` were split for the same reason, and
`readability-function-size` now holds the line.

### Delete code

The cheapest code to maintain is code that does not exist. It has no bugs, needs
no tests, and never appears in a profile.

So: do not build the vessel plugin API before you have two vessels. Do not write
the generic scenario serialiser before you have two scenario types. Speculative
generality is the most expensive kind of code, because you pay to maintain an
abstraction that is not yet shaped by any real requirement — and when the second
case finally arrives, it never fits the abstraction you guessed at.

Write the specific thing twice. The right abstraction is obvious on the third.

### Build time is a feature

Every second on the edit-compile-run loop is a tax on every change you will ever
make, and it compounds silently until one day the project feels slow to work on
and nobody can say why.

- **Keep heavy headers out of headers.** `VulkanContext.hpp` forward-declares
  `struct SDL_Window;` instead of including SDL. That is exactly right, and it
  is why the physics does not pay for SDL. Keep doing that.
- **`ccache`.** You are rebuilding SDL3 from source. Do this one first.
- **Measure it**, because rule 8 of this document applies to build time too.
  `clang -ftime-trace` produces a profile you can open in a browser and see
  precisely which header is costing you.

### Write down why, not just what

Your inline comments are genuinely good at this — the note on why whole
revolutions get folded out before the Newton iteration, the one on reverse-Z.
Those save a reader an hour each.

But some decisions are bigger than any one file: pinning Vulkan headers instead
of using the installed SDK, targeting C++23, choosing error codes over
exceptions, reverse-Z. Those belong in short **architecture decision records** —
a `docs/adr/` folder, one file per decision, three paragraphs each: what we
decided, what we considered, why. Two years from now somebody will ask "why are
we pinning these headers?" and the answer will exist instead of being
reconstructed from a stale memory.

---

## 18. Portability

You are Windows-only today. But look at what you chose: CMake, clang, SDL3,
Vulkan. Every one of those is cross-platform, and that was not an accident. You
have already paid for portability. Do not throw it away by accident.

And to be clear about the motive: **a Linux CI job is not about shipping on
Linux.** It is about getting a second compiler, a second standard library, and
UndefinedBehaviorSanitizer and ThreadSanitizer that actually work — the ones
Windows cannot fully give you (section 1). Portability is a bug-finding tool
that happens to also let you ship elsewhere.

- **Know your types.** Container sizes and indices are `size_t`. Vulkan hands
  you `uint32_t`. Mixing them is exactly where 32-bit-versus-64-bit bugs hide,
  and they only appear when a count gets large. `-Wsign-conversion` and
  `-Wconversion` find these; that is a second reason to keep them on.
- **Use `std::filesystem::path` for paths, not `std::string`.** This used to
  read:

  ```cpp
  VkShaderModule loadShaderModule(const std::string& path, std::string& error) const;
  ```

  On Windows, paths are natively `wchar_t`. A `std::string` path works fine
  until a user's account is named something outside your narrow encoding, and
  then it fails in a way that is miserable to diagnose from a bug report.
  `std::filesystem::path` handles the platform's native encoding for you and
  costs nothing to adopt now.
- **`std::jthread`, never `pthread` or Win32 threads.** Same argument as
  section 13.
- **Avoid non-`const` statics.** Initialization order across translation units
  is unspecified, and every mutable static is a data race waiting for you to add
  a thread. If you need shared state, pass it.
- **Do not let platform-isms leak upward.** Anything Windows-specific belongs
  behind the renderer or platform layer, never in `orbsim_core`. Section 12 is
  the same rule; portability is one of the things it buys you.

---

## 19. Determinism, because this is a simulator

This one is not in the Core Guidelines, because it is a property of your domain
rather than of C++. It matters enormously here and it is very expensive to
retrofit.

Orbiter lets you save a scenario and resume it later, and expects the flight to
continue identically. That is a promise about determinism. If you want to make
that promise — and for a simulator you almost certainly do — the time to decide
is now, while it is nearly free.

- **Fixed timestep for the physics, decoupled from the render rate.** Feed the
  integrator a constant `dt` and accumulate leftover frame time. If you instead
  hand the integrator whatever the last frame took, the trajectory becomes a
  function of frame rate: a slow machine flies a measurably different orbit than
  a fast one, and neither can reproduce the other. That is not a bug you find,
  it is a bug you argue about.
- **Interpolate for rendering, integrate on the fixed clock.** The renderer
  reading between the last two physics states is what makes a fixed timestep
  look smooth.
- **Seed every RNG explicitly and save the seed in the scenario.** An unseeded
  or time-seeded generator makes a replay a different flight.
- **Floating point is reproducible within one binary, not across binaries.**
  Same compiler, same flags, same CPU gives bit-identical results. Change the
  optimization level, or `-ffp-contract`, or the target architecture, and it
  will drift. This is the deeper reason section 11 tells you not to touch those
  flags. If bit-identical replay across machines ever becomes a requirement,
  understand that it is a large commitment involving pinned flags and possibly
  soft-float — decide that deliberately, do not promise it casually.
- **Test it.** Run a scenario twice from the same initial state and assert the
  results are bit-identical. It is a cheap test and it catches a whole class of
  accidental nondeterminism: iteration over an unordered container, uninitialized
  padding, a branch on wall-clock time, an uninitialised read that happened to be
  benign. You already cross-validate two propagators; this is the same instinct
  applied to the simulation as a whole.

---

## 20. What actual flight software does

In 2006 Gerard Holzmann, of NASA/JPL's Laboratory for Reliable Software,
published **"The Power of 10: Rules for Developing Safety-Critical Code"** — the
rules JPL applies to software that actually flies. You are writing a game *about*
spaceflight rather than spaceflight software, so most of these are stricter than
you need. But it is worth knowing where the line is, and a few of them land
squarely on code you have already written.

**Transfers directly:**

- **Rule 10: compile with all warnings at the most pedantic setting, from day
  one.** Identical to section 1. They put it in the top ten; so should you.
- **Rule 6: declare data at the smallest possible scope.** ES.5.
- **Rule 7: check the return value of every non-void function.** This is what
  `[[nodiscard]]` mechanises for you (section 5).
- **Rule 5: at least two assertions per function, checking things that "cannot
  happen".** This is section 2's precondition argument, stated as a quota. The
  quota is not the point; the habit is.
- **Rule 4: no function longer than about sixty lines.** `VulkanContext::init()`
  was 149; it is 29 now, and `readability-function-size` keeps it there.

**Transfers with judgement:**

- **Rule 3: no dynamic allocation after initialization.** Too strict for a game
  that will stream terrain. But the frame-loop version of it is exactly right:
  **do not allocate in the hot path.** Size your vertex buffers once and reuse
  them.
- **Rule 2: every loop must have a provable fixed upper bound.** The Newton
  iterations always satisfied this — 100, 100, and 200 in `Orbit.cpp` — which is
  genuinely good practice that most numerical code skips.

  But rule 2 is meant to be paired with rule 5, and that half used to be
  missing:

  ```cpp
  for (int i = 0; i < 100; ++i) {
      const f64 dE = -(E - ecc * std::sin(E) - M) / (1.0 - ecc * std::cos(E));
      E += dE;
      if (std::abs(dE) < 1e-14) break;
  }
  return E;    // converged? did not converge? the caller cannot tell.
  ```

  At an eccentricity of 0.9999 that returned a number which was simply wrong,
  with nothing anywhere reporting it. The bound was the easy half. **Say
  something when the loop exits without converging.**

  **Fixed:** all three now return `std::expected<_, OrbitError>` and end with
  `std::unexpected(OrbitError::SolverDidNotConverge)`. A silent wrong answer in a
  Kepler solver becomes a spacecraft in the wrong place twenty minutes later, for
  no visible reason.

**Does not transfer:**

- **Rule 1: no recursion.** You will want recursion for quadtree terrain, and
  that is fine. JPL bans it because they need static stack-depth bounds; you do
  not.
- **Rule 9: at most one level of pointer dereference.** Written for C in 2006.
  Modern C++ ownership types are a better answer to the same problem.

The reason to read these is not to adopt them wholesale. It is the instinct
underneath all ten: **JPL found that code a tool cannot analyse is code you
cannot trust.** Every rule is chosen to make static analysis possible. That is
the same instinct as section 1, arriving from an industry where being wrong
costs a spacecraft.

---

## 21. Non-rules and myths

The Core Guidelines have a section called NR, and I love that it exists, because
half of what gets called a "coding standard" is superstition that was promoted
to policy. If someone hands you one of these in review, push back.

- **NR.1: declarations do not have to go at the top of a function.** That is a C
  habit from before you could declare a variable anywhere. Declare things at
  first use, in the smallest scope that works (ES.5, ES.22).
- **NR.2: a function may have more than one `return`.** Early returns for
  preconditions make code *shorter* and *flatter*. The single-return rule comes
  from a language without destructors. You have destructors.
- **NR.3: do not avoid exceptions.** If you land on `std::expected` after
  reading section 7, land there because you weighed it, not because somebody
  told you once that exceptions are slow. Measure it or drop the claim (Per.6).
- **"Complicated code is faster."** Per.4 says no. The optimizer understands
  simple code and gives up on clever code. This is what Compiler Explorer is
  for.
- **"Low-level code is faster."** Per.5 says no. `std::sort` beats your
  hand-rolled quicksort. `std::ranges::transform` compiles to the loop you would
  have written, minus the off-by-one error you would have written with it.
- **"Abstractions cost performance."** This is the one I care about most, and it
  is why I keep showing people C++ running on a Commodore 64. Zero-overhead
  abstraction is not marketing. Write the `Radians` type from section 2, look at
  the generated assembly, and watch it vanish.

The pattern in all of these: **the myth is a claim about the compiler, the
compiler is right there, and it will answer you.** Go ask it.

---

## Appendix A: The Toolbox

Here is the whole list. You do not need all of it on day one — but you should
know what exists, because the worst outcome is spending a week debugging
something a tool would have handed you in nine seconds.

Two rules before the list:

**Rule one: compile with at least two compilers.** I mean this. Different
compilers implement different warnings, make different assumptions, and are
wrong about different things. Where two compilers disagree about your code is
almost exactly where your bugs live. Building with a second compiler is the
highest-value tooling change most projects can make, and it costs you one CI
job.

**Rule two: the tool you do not run in CI is a tool you do not have.** A linter
someone runs manually when they remember is not a linter. It is a good
intention.

Items marked ✅ are already on this machine.

### Build and configure

| Tool | Notes |
|---|---|
| **CMake** ✅ | 3.31.2 on PATH, but CLion bundles **4.3.1** — use the bundled one, it is what your IDE uses |
| **Ninja** ✅ | 1.12.0. Fast, and the only generator worth using here |
| **CMakePresets** ✅ | Already pinning clang. This is how you stop arguing about build flags |
| **FetchContent** ✅ | Currently pulling SDL3, vk-bootstrap, VMA, Vulkan-Headers |
| **ccache** / **sccache** | Compile caching. You are rebuilding SDL3 from source; you will want this |
| **CPM.cmake** | A nicer wrapper over FetchContent if dependency handling gets busy |

### Compilers — plural, on purpose

| Tool | Notes |
|---|---|
| **clang 22** ✅ | Your primary. Targets `x86_64-pc-windows-msvc` |
| **MSVC 14.44** ✅ | VS2022 is installed. This is your free second opinion — add a CI job |
| **GCC** | MinGW 13.2 is installed ✅ but **lacks `<print>`** and cannot build this project. You need GCC 14+, realistically via a Linux CI job |
| **Compiler Explorer** | godbolt.org. When you wonder whether the optimizer did the thing, stop wondering and go look |
| **C++ Insights** | cppinsights.io. Shows you what the compiler *actually* generated from your template or range-for. Wonderful teaching tool, wonderful debugging tool |

### Static analysis

| Tool | Notes |
|---|---|
| **clang-tidy** ✅ | Ships with clang. You already generate `compile_commands.json`, which was the hard part. Start with `bugprone-*`, `performance-*`, `readability-*` |
| **cppcheck** | Finds a genuinely different set of things than clang-tidy. Run both |
| **clang static analyzer** | Path-sensitive, deeper than clang-tidy, slower. Worth a nightly job |
| **include-what-you-use** | Your headers will accumulate junk includes. IWYU is the only thing that reliably cleans them |
| **MSVC `/analyze`** | Free with the compiler you already have |

### Dynamic analysis

| Tool | Notes |
|---|---|
| **AddressSanitizer** | `-fsanitize=address`. Works with clang on this toolchain. Set up a build config for it *today* |
| **UndefinedBehaviorSanitizer** | Only partial support on Windows. Full value arrives when you add a Linux CI job |
| **ThreadSanitizer** | Not yet — but the moment you thread the physics off the render loop, this is mandatory |
| **Application Verifier** | Ships with the Windows SDK. Catches handle and heap misuse ASan does not |
| **Dr. Memory** | Valgrind-shaped tool that actually runs on Windows |

### Testing, fuzzing, coverage

| Tool | Notes |
|---|---|
| **CTest** ✅ | Already wired up |
| **Catch2** / **doctest** | Your test harness is currently hand-rolled. That was the right call for one file. At three or four files you will want real failure output, test filtering, and tagging — switch then, not before |
| **libFuzzer** | `-fsanitize=fuzzer`. Underused by almost everybody, and **a superb fit for this project**: throw random state vectors at `elementsFromState`, round-trip them, assert the invariants hold. The fuzzer will find the degenerate orbit you did not think of. It always does |
| **llvm-cov** / **llvm-profdata** ✅ | Ships with clang. Coverage is a map of what you have *not* tested |
| **OpenCppCoverage** | Windows-native alternative if the llvm route annoys you |

### Performance

| Tool | Notes |
|---|---|
| **Tracy** | Frame profiler with CPU *and* GPU timing and a real timeline view. For a realtime simulator this is the single most valuable profiler you can add. Do this one |
| **Google Benchmark** / **nanobench** | For microbenchmarks. nanobench is dramatically easier to drop in |
| **quick-bench.com** | Two implementations, one graph, thirty seconds. No excuse not to check |
| **NVIDIA Nsight Systems** | You have an RTX A2000. Use it |
| **Superluminal** / **Intel VTune** | Commercial sampling profilers, both excellent |

### Graphics — the ones this project specifically needs

| Tool | Notes |
|---|---|
| **Validation Layers** ✅ | Already reachable via `orbsim.exe --validate` |
| **Synchronization Validation** | **Not on by default — turn it on.** The base layers do not catch read-after-write hazards. Given that `transitionImage()` currently uses conservative `ALL_COMMANDS` barriers, this is exactly the layer that will tell you when you tighten them incorrectly |
| **GPU-Assisted Validation** | Catches out-of-bounds descriptor access at runtime. Slow. Worth it periodically |
| **vkconfig** ✅ | In the SDK. This is how you switch the above on without touching code |
| **RenderDoc** | Frame capture and step-through. When something renders black, this answers "why" in about a minute |
| **NVIDIA Nsight Graphics** | Deeper GPU-side analysis than RenderDoc, and you have the hardware for it |
| **glslc** ✅ / **glslangValidator** ✅ | Already compiling your shaders |
| **spirv-val**, **spirv-opt**, **spirv-cross** ✅ | Validate, optimize, and decompile SPIR-V. `spirv-cross` is how you check what the driver is really seeing |
| **vulkaninfo** ✅ | Device caps and, as you already discovered, which GPU you are actually getting |

### Hygiene

| Tool | Notes |
|---|---|
| **clang-format** ✅ | Ships with clang. Write the config, check it in, stop discussing formatting forever |
| **clangd** ✅ | Already configured against your compile database |
| **pre-commit** | Runs format and lint before the bad commit exists rather than after |
| **GitHub Actions** | Where rule two gets enforced. Matrix it across clang and MSVC, Debug and Release, sanitizers on |

---

## Appendix B: Cross-reference with the C++ Core Guidelines

This document is opinionated and short. The
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines.html)
by Bjarne Stroustrup and Herb Sutter are neither, and that is a compliment -
they are the reference, and you should keep them open in a tab.

Here is how the sections above map onto them, so you can find the long version
of any argument.

| This document | Core Guidelines |
|---|---|
| 1. Use the tools available | P.12; Appendix D (Supporting tools) |
| 2. Express intent, hard-to-misuse interfaces | P.1, P.3, P.4, P.5, I.1, I.4, I.5, I.6, I.7, I.8, I.23, I.24 |
| 3. Make it `constexpr` | P.5, Per.11 |
| 4. Rule of Zero | R.1, P.8, P.11, E.6, NR.5 |
| 5. `const` and `[[nodiscard]]` | Con.1, Con.2, Con.3, Con.4, P.10 |
| 6. Initialize your variables | ES.20, ES.21, ES.22 |
| 7. Error handling | E.1-E.8, I.10, NR.3 |
| 8. Things we simply do not do | ES.30, ES.31, ES.35, ES.45, ES.47, ES.48, ES.49, ES.50, R.3, R.5, R.11, R.20, R.21, SF.7, Enum.3 |
| 9. Prefer algorithms over raw loops | ES.1, ES.2, ES.71 |
| 10. Measure. Do not guess. | Per.1-Per.7, Per.15 |
| 11. Floating point | ES.46, P.4 |
| 12. Keep the layers apart | P.11; section A (Architectural ideas) |
| 13. Concurrency | CP.1, CP.2, CP.3, CP.4, CP.20, CP.31, CP.40, CP.44, CP.50, CP.51 |
| 14. Source files | SF.5, SF.7, SF.9, SF.11, ES.4 |
| 15. Naming and formatting | NL.1-NL.9, ES.7, ES.8 |
| 16. Comments | NL.1, NL.2 |
| 17. Maintainability | F.2, F.3, ES.5, ES.45 |
| 18. Portability | P.2, ES.46, CP.51 |
| 19. Determinism | no direct rule; nearest are Per.19 and CP.3 |
| 20. What flight software does | P.5, P.7, ES.5; JPL Power of Ten |
| 21. Non-rules and myths | NR.1, NR.2, NR.3, Per.4, Per.5 |

### Deliberately not covered yet

Not because the rules are wrong, but because this codebase does not yet contain
the code they apply to. Revisit each when that changes.

- **T (Templates and generic programming).** There is almost no generic code here
  yet. When you write your first real template, read T.1-T.25 first, and use
  **concepts** to constrain it (I.9). C++23 gives you them, and unconstrained
  templates produce exactly the error messages that give C++ its reputation.
- **C (Classes and class hierarchies).** Barely applicable, because there is
  essentially no inheritance in this codebase. That is not an oversight - it is
  the Core Guidelines' own preferred answer. When you build the vessel system
  the temptation to write `class Vessel : public Body` will be strong; read
  C.120-C.133 before you give in, and consider composition first.
- **CPL (C-style programming).** None present. Keep it that way.
- **Pro (Profiles) and GSL.** The type, bounds and lifetime safety profiles are
  worth reading as a preview of where the language is heading. `gsl::not_null`
  and `Expects`/`Ensures` are available today if you want them; `std::span`
  already covers the bounds case.

### Mechanical enforcement

A useful chunk of the above is checkable by machine rather than by review:

```
clang-tidy --checks='cppcoreguidelines-*,bugprone-*,performance-*'
```

That will not enforce all of it - no tool can check whether an interface
expresses intent - but it enforces the boring half automatically, which leaves
review time for the half that genuinely needs a human. That is rule two from the
Toolbox, applied to this appendix.

---

## Appendix C: Where this came from

Read these. This document is a distillation with opinions attached; they are the
real thing.

- **[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines.html)**
  — Stroustrup and Sutter. The reference. Mapped section by section in
  Appendix B.
- **[C++ Best Practices](https://github.com/cpp-best-practices/cppbestpractices)**
  — the collaborative collection. Its chapter structure (Tools, Style, Safety,
  Maintainability, Portability, Threadability, Performance, Correctness) is
  roughly the shape of this document, and sections 17 and 18 exist because that
  book had chapters this one was missing. The Style, Safety, Maintainability,
  Portability and Correctness chapters were each read against this document;
  the boolean-parameter rule, the `assert` side-effect trap, the reserved-
  identifier rule and the note on passing primitives by value all came from
  that pass.
- **[strong_type](https://github.com/rollbear/strong_type)**,
  **[NamedType](https://github.com/joboccara/NamedType)**,
  **[type_safe](https://foonathan.net/type_safe/)** — if you would rather not
  hand-roll the `Radians`/`Degrees` boilerplate from section 2.
- **"The Power of 10: Rules for Developing Safety-Critical Code"** — Gerard J.
  Holzmann, NASA/JPL Laboratory for Reliable Software, 2006. Section 20.
- **[cppreference.com](https://en.cppreference.com/)** — the standard library
  reference that is actually correct. Not the other one.
- **[Compiler Explorer](https://godbolt.org/)** — settles arguments about what
  the optimizer does, in about ten seconds.
- **[C++ Insights](https://cppinsights.io/)** — shows you what the compiler
  expanded your code into.
- **"C++ Seasoning"**, Sean Parent — the source of "no raw loops" (section 9).
- **"Large-Scale C++ Software Design"**, John Lakos — physical design, and why
  your header structure decides your build time (section 17).

---

## Before you push

- [ ] Builds clean. Zero warnings. Not "only the usual ones" — zero.
- [ ] `ctest` is green.
- [ ] New logic has a test, and ideally one that checks it against something
      independent.
- [ ] Everything that can be `const` is `const`.
- [ ] Everything that can be `constexpr` is, with a `static_assert` proving it.
- [ ] No new raw owning pointers, and no new hand-written destructors.
- [ ] New public functions state their preconditions.
- [ ] No error a caller can silently ignore.
- [ ] Adjacent parameters cannot be swapped without the compiler noticing.
- [ ] No new boolean parameters. Use an `enum class`.
- [ ] Every loop is bounded, and says something when it does not converge.
- [ ] It is committed. You did put this under version control, right?
- [ ] The renderer still doesn't leak into the physics.

---

That's it. Go make the compiler yell at you.

Keep coding, and I'll see you next time.
