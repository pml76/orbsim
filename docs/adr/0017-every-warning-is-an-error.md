# ADR 0017: Every warning the compiler has, as an error

Status: accepted (2026-09-11)

The owner's rule, in their words: *"I like to have as many warnings (as
errors!) as possible. I want them to rule out old and bad coding style. If
these errors are issued regarding interfacing library code, they should be
disabled locally (and documented why these were disabled)."* This record is
how that is carried out. It replaces the hand-picked list of fifteen warnings
that [`../../CODING_GUIDELINES.md`](../../CODING_GUIDELINES.md) section 1
started from, and it extends [0005](0005-correctness-is-enforced-by-tools.md)
rather than superseding it.

## Decision

- **clang: `-Weverything -Werror`**, less three project-wide exceptions, each
  written beside its flag in `CMakeLists.txt` with its reason:
  - the **backward-compatibility groups**, `-Wc++98-compat` through
    `-Wpre-c++23-compat-pedantic`. They report every feature newer than the
    standard they name, for code that must still build under it. This project
    is C++23, and the aim is to rule out *old* style;
  - **`-Wswitch-default`**. Our switches over our own enums are exhaustive
    with no `default:`, so that `-Wswitch` reports a newly added enumerator. A
    `default:` would silence that, and `-Wcovered-switch-default`, also on,
    would then report the default. The two cannot both be satisfied;
  - **`-Wpadded`**, which reports struct layout rather than style or
    correctness. Its fixes are reordering members away from their logical
    order, or adding dead fields. Ten sites when measured.
- **gcc: every warning gcc lists for C++**, because gcc has no `-Weverything`.
  `scripts/gcc-warnings.py` asks the compiler (`-Q --help=warning,c++`),
  compiles a trivial program with each candidate to drop any it rejects, and
  writes `cmake/GccWarnings.cmake`: 172 flags from g++-14 14.3.0, with each
  levelled warning at its strictest level (`-Wimplicit-fallthrough=5`,
  `-Wcast-align=strict`, ...). Re-run it on a gcc upgrade. Switched off, each
  with its reason in the script:
  - `-Wsystem-headers`, which reports inside the dependencies;
  - `-Wabi`, which is about linking against other gcc versions' output;
  - `-Wnamespaces`, `-Wtemplates`, `-Wmultiple-inheritance` and
    `-Wvirtual-inheritance`, which report every use of a feature, for style
    guides that ban it;
  - `-Wswitch-default` and `-Wpadded`, as for clang;
  - `-Weffc++`, the 1998 *Effective C++*. On this code its only reports are
    the aggregates `Elements` and `OrbitInfo`. Their members initialize
    themselves, as section 6 of the guidelines asks, and the warning wants
    them named in a member initializer list, which needs a constructor these
    aggregates do not have.

  Warnings that need a threshold (`-Wlarger-than=`, `-Wstack-usage=`, ...)
  stay off, because any number chosen would be arbitrary.
- **Our own code is fixed, not silenced.**
- **A warning raised where our code meets a library's interface is switched
  off at that site alone**, with `#pragma clang diagnostic push`, `ignored`
  and `pop`, and the reason written beside it. A gcc-only warning takes the
  gcc form inside `#if defined(__GNUC__) && !defined(__clang__)`, because
  clang rejects a warning name it does not know. The sites, when this was
  written:

  | Where | Warning | The interface |
  |---|---|---|
  | `render/VulkanHandle.hpp` | `-Wcast-function-type-strict` | `vkGetInstanceProcAddr` returns a generic function pointer; the specification says to cast it |
  | `render/VulkanContext.cpp` | `-Wswitch-enum` | `VkResult` grows with every header release; `resultName` names what it can meet |
  | `render/VulkanContext.cpp` | `-Wunsafe-buffer-usage-in-format-attr-call` | `SDL_Log` of the loader's C string |
  | `render/VulkanContext.cpp` | `-Wunsafe-buffer-usage-in-container` | SDL returns its extensions as a pointer and a count |
  | `render/VulkanContext.cpp`, twice | `-Wunsafe-buffer-usage-in-libc-call` | `memcpy` into memory VMA mapped |
  | `render/VulkanContext.cpp`, the file | `-Wmissing-designated-field-initializers` | Vulkan's and VMA's C structs, 28 initializers that leave the unused fields zero |
  | `app/main.cpp`, `main` | `-Wunsafe-buffer-usage-in-container` and `-Wunsafe-buffer-usage-in-libc-call` | `argv` and `argc`; `std::fputs`, which cannot throw |
  | `tests/fuzz_orbit.cpp` | `-Wmissing-prototypes`, `-Wunsafe-buffer-usage-in-libc-call` | libFuzzer's entry point, which no header declares, and its input pointer |
  | `tests/OrbitTestSupport.hpp` and `.cpp`, `tests/test_time.cpp` (gcc) | `-Wabi-tag` | Catch2's `describe()` and `StringMaker::convert`, which return `std::string` because Catch2 declares them so |

  The designated-initializer warning used to be off for the whole project,
  for Vulkan's sake; it is now off only in the file that fills Vulkan's
  structs in, and on for everything else. The cost, written there too: a
  struct of our own initialized in that file goes unchecked.
- **Switching a warning off project-wide is the owner's decision**, as a
  clang-tidy suppression is (working agreements 1 and 7 in
  [`../../CLAUDE.md`](../../CLAUDE.md)).
- **Third-party code is not judged**, which is unchanged. Dependencies'
  headers are `SYSTEM`. The dependencies built from source are their own
  targets with their own flags. `render/VmaImpl.cpp`, which only instantiates
  VMA's implementation, builds with `-Wno-everything`.
- **The floating-point `==` is gone from the value types.** A defaulted `<=>`
  brings a defaulted `==` with it, so every unit type, `Vec3` and `Quat` handed
  out exactly the comparison section 11 forbids -- and clang's `-Wfloat-equal`
  reports it. `Quantity` keeps its ordering and deletes `==`. An exact
  equality is spelled `nearlyEqual(a, b, Tolerance{0.0})`. A determinism check,
  where bit identity is the claim, says so: `bitIdentical()` on `Quantity`,
  `Vec3` and `Quat`, through `std::bit_cast`, which also tells +0.0 from -0.0.
  `TimePoint` keeps `==` and `<` and compares its day with
  `std::strong_order`. Over every day an instant can hold, which is whole,
  finite and never -0.0, that is the numerical order. The one exception is the
  NaN a violated precondition leaves in a Release build, which now compares
  equal to itself and after every instant. This refines the phrase in
  [0011](0011-the-integrator-has-three-seams.md) and
  [`../VERIFICATION.md`](../VERIFICATION.md) rule 16, "the one place `==` on
  floats is correct": it is the one place an exact comparison is correct, and
  its name is `bitIdentical()`.

## What it found

Measured over our sixteen translation units before anything was switched
off: **2,818 distinct warnings, 2,705 of them from the backward-compatibility
groups.** What was left after the three exceptions was mostly worth having:

- the float `==` above: the defaulted comparisons under clang, and three
  exact-zero tests in `orbit/Orbit.cpp` under gcc, now
  `std::fpclassify(x) == FP_ZERO`, which agrees with `x == 0.0` on every
  input;
- **16 missing `[[clang::lifetimebound]]`**: every handle wrapper in
  `render/VulkanHandle.hpp` refers to the driver object its constructor was
  handed, and `deviceName()` returns a reference into its context;
- a copy the compiler could not elide (`-Wnrvo`), and three files without a
  final newline;
- on Linux only, **`-Wweak-vtables`**: the test matchers were polymorphic
  and entirely inline, so each of the four translation units that included
  them emitted their vtables. Their `describe()` now lives in
  `tests/OrbitTestSupport.cpp`, the key function, and the test support is a
  static library. The Itanium ABI has key functions and MSVC's does not, so
  Windows cannot see this warning. The Linux presets can.

## What we considered

**The hand-picked list** the project had: `-Wall -Wextra -Wpedantic` and
twelve more. It catches what somebody thought to pick, and nobody picked
`-Wfloat-equal`, so every unit type carried a floating-point `==` and nothing
said so.

**`-Weverything` with no exceptions.** The compatibility groups alone report
2,705 uses of C++11 to C++23, which would rule out the new style rather than
the old. `-Wswitch-default` against `-Wcovered-switch-default` has no
answer at all.

**Library-interface warnings off project-wide**, as the designated
initializers were. One flag instead of a dozen pragmas, and the same warning
is then silent in our own code too. The owner's rule is the site.

**For the Linux findings**, two alternatives were put to the owner. For
`-Wweak-vtables`, a pragma instead of the key function: a weak reason,
because Catch2 makes a matcher virtual, but we chose to keep it header-only.
For gcc's `-Wabi-tag`, excluding it next to `-Wabi`, since it reports an ABI
tag gcc applies by itself, or writing `[[gnu::abi_tag("cxx11")]]` at the
sites. The owner chose the literal rule, the pragma at each site.

## Why

[0005](0005-correctness-is-enforced-by-tools.md) and section 1 of the
guidelines: the compiler is the cheapest reviewer there is, and a reviewer
told to look at everything misses less than one told what to look for. The
cost is the one clang-tidy's family list already carries on purpose. A new
compiler brings new warnings, and they arrive as errors to be triaged, which
is the design working.

## What this record does not decide

- **How ERFA's C sources are compiled.** [M1-05](../plan/tasks/m1-05-tdb-and-ut1.md)
  pins it and settles that. The precedent is VMA's: third-party sources are
  not ours to fix, nor ours to hear about.
- **The worked example** in `coding-guidelines-example/` takes the same
  policy in a commit of its own.
- **`tests/fuzz_orbit.cpp` has never been linted.** It is missing from the
  lint list, and Windows has no compile command for it; clang-tidy in the
  `linux-fuzz` tree reports 13 findings. Found while this was done, and
  decided as a follow-up commit.
