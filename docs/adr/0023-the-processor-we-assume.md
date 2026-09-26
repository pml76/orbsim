# ADR 0023: The processor this project assumes

Status: **accepted** (2026-09-26), asked for by the owner during
[M1-16](../plan/tasks/m1-16-probe-mode.md). Register decisions 200, 201 and 202.

## Decision

**orbsim is built for an x86-64 processor with MMX, SSE, SSE2, SSE3, SSSE3,
SSE4.1, SSE4.2, FMA3, AVX, AVX2 and F16C** -- what this project's development
machine, an Intel Core i9-12900H, and AMD's Zen 2 have in common -- and the
compiler is told so for every file it compiles, this project's and every
dependency's:

| Compiler | Flags |
|---|---|
| clang, gcc | `-mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mfma -mavx -mavx2 -mf16c -ffp-contract=off` |
| MSVC | `/arch:AVX2 /fp:precise` |

They are set with `add_compile_options` near the top of `CMakeLists.txt`,
before the first target and the first `FetchContent`, because a directory's
compile options reach only the targets and subprojects added after them. The
worked example sets the same flags in its own `CMakeLists.txt`.

Four things about the list are part of the decision.

**SSE4A is left out, although the owner's list named it.** It is AMD's
extension -- LLVM's model of Zen 2 has it and of Alder Lake does not -- and this project's development
machine, an Intel Core i9-12900H, does not have it -- measured on 2026-09-26
from Windows and from `/proc/cpuinfo` under WSL. clang accepts `-msse4a`, and a
build allowed to emit its instructions could stop with an illegal instruction
on the very machine it is developed on.

**"MMX-plus" has no flag.** It is AMD's name for a handful of MMX additions
that Intel ships as part of SSE, which the flags already cover; clang has no
switch for the AMD name (it rejects `-m3dnowa`, the nearest).

**F16C was added the same day** (decision 202), once it came up while M1-16's
half-float conversion was being written: it is the hardware conversion between
16- and 32-bit floats. Both processors have it -- measured from this machine's
`/proc/cpuinfo` under WSL, and from LLVM's own model of each processor
(`clang -march=znver2` and `-march=alderlake` both define `__F16C__`; only
Zen 2's defines `__SSE4A__`, which is the reason above in one line). MSVC's
`/arch:AVX2` covers it. Worth knowing: the hardware conversion turns a
signalling NaN quiet, where a software conversion keeps its bits.

**There is no run-time check.** On a processor without these extensions the
program stops with an illegal instruction rather than a message, and that is
accepted: a check cannot be guaranteed to run before the first such
instruction, because libraries execute code before `main`.

## Multiply-add fusion, which the flags would otherwise have changed

FMA3 lets the processor compute `a * b + c` with a single rounding, and
**clang fuses that expression by default once `-mfma` is on** -- measured on
2026-09-26: no fused instruction without the flag, one with it. This project's
own targets have forbidden fusion since the start, through `orbsim_fp`
([CODING_GUIDELINES.md](../../CODING_GUIDELINES.md) section 11), but that target
reaches only code that links the core. Third-party code did not have it, and
among that code is Catch2, whose `WithinAbs` and `WithinRel` comparisons -- used
168 times in `tests/` -- are computed in its own compiled library. So
**`-ffp-contract=off` is set for everything, beside the instruction set**, and
the arithmetic of every file stays what it was: only the instructions carrying
it change.

MSVC 19.51 does not fuse under `/fp:precise` unless `/fp:contract` is also
given (measured the same day: none without it, one with it), and `/fp:precise`
is its default; it is set explicitly so that the rule reads the same for all
three compilers.

## What we considered

**Leaving the compiler at its baseline**, x86-64 with SSE2, which is what the
project built for until this record. The owner chose to assume the newer set.

**`-march=x86-64-v3`**, the standard level that roughly matches the list. It
also enables BMI1, BMI2, LZCNT and MOVBE, which the owner did not name,
so the extensions are listed one by one instead.

**Setting SSE4A as named**, which risks an illegal instruction on this machine,
and **leaving third-party code free to fuse**, which would have changed
Catch2's arithmetic without anyone asking for it.

## How it was checked

Before and after, in both build trees: `check` green, and every Catch2 suite
reporting the same number of assertions passed. The Linux presets
(`linux-sanitize`, `linux-gcc`) and `windows-msvc` were run with the new flags
too. [`../HISTORY.md`](../HISTORY.md) has the numbers.
