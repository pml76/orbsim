# orbsim — project state and handoff

Kind: reference
Binding: no
Read when: you are picking this project up on another machine or after a gap,
or you hit something odd and want to know whether it is known.

This file exists so the project can be picked up on a different machine, or
after a gap, without reconstructing anything from memory. It records what was
decided, what is *not* decided, and how to get a machine ready.
[`CLAUDE.md`](../CLAUDE.md) says how to work here; this says where things
stand.

Two things were split out of it on 2026-09-09, keeping their original section
numbers so existing references still resolve:

- **The numbers** — versions, counts, what is built, what is next — are in
  [`STATUS.md`](STATUS.md), which is the only place any of them appears.
- **The narrative** — sections 3 and 4, the sessions and the bug that
  justified one — is in [`HISTORY.md`](HISTORY.md).

## Contents

- [1. Where the project is](#1-where-the-project-is) — pointer to `STATUS.md`
- [2. What this machine has](#2-what-this-machine-has) — the install recipes,
  Windows and WSL
- [3 and 4](#3-and-4-the-change-history) — moved to `HISTORY.md`
- [5. Decisions taken, and where they are written down](#5-decisions-taken-and-where-they-are-written-down)
- [6. Decisions that were reviewed, and how they landed](#6-decisions-that-were-reviewed-and-how-they-landed)
- [7. Open questions — for the project owner, not for me](#7-open-questions--for-the-project-owner-not-for-me)
- [8. Gotchas worth not rediscovering](#8-gotchas-worth-not-rediscovering)
- [9. If you are picking this up cold](#9-if-you-are-picking-this-up-cold)

---

## 1. Where the project is

**In [`STATUS.md`](STATUS.md).** What is built, what each directory contains,
the test suites and their counts, the current milestone and the next task all
live there, because they are the facts that change most often and they must
change in exactly one place.

---

## 2. What this machine has

**The versions are in [`STATUS.md`](STATUS.md).** This section is how to
install and run them.

**The renderer now requires a discrete GPU where one exists, and gets the RTX
A2000.** Both devices satisfy every requirement, and until 2026-09-06 the Intel
UHD won because vk-bootstrap's discrete *preference* is not binding while
`allow_any_gpu_device_type` is left at its default of true. A machine with only
an integrated GPU still runs, via a logged fallback.

A visible side effect: the frame rate went from 60 to about 1000 fps. That is
not the A2000 being seventeen times faster at clearing a screen — the Intel
driver does not expose `VK_PRESENT_MODE_MAILBOX_KHR`, so it fell back to FIFO
and blocked on vsync, while NVIDIA offers mailbox and does not. Worth knowing
before reading anything into an fps number on this project.

**The Orbiter reference clone is at `C:\Reference\orbiter`**, outside this
repository, ~1.1 GB, shallow. It is not required to build. The licence
boundaries are in `CLAUDE.md`; the tile format spec is
`Doc/Orbiter Developer Manual/PLANETS.tex`, section `sssec:tile_file_layout`.

**Planetary imagery** lives in `data/textures/`, gitignored. Three Blue Marble
files (~29 MB) are already downloaded on this machine; `data/textures/README.md`
has the exact URLs to fetch them again.

Git remote: `git@github.com:pml76/orbsim.git`. The repository is **public**,
which is why commit messages carry co-authorship but never a session URL.

### Starting on a new machine

```
git clone git@github.com:pml76/orbsim.git
cd orbsim
git config core.hooksPath scripts/git-hooks      # enables the pre-commit format check
cmake --preset relwithdebinfo
cmake --preset debug
cmake --build build/relwithdebinfo --target check
cmake --build build/debug --target check
```

Needs clang 17+ (23 here), CMake 3.28+, Ninja, Python 3, and a Vulkan SDK for
the loader and `glslc`. Everything else is fetched and pinned by CMake. Without
a Vulkan SDK, `-DORBSIM_BUILD_APP=OFF` builds the core and its tests. Python is
not optional: `check` runs `scripts/check-doc-links.py` and the Horizons
converter's golden test, and both fail rather than pass when it is missing.

**Then generate the reference data, once per machine** (since 2026-09-19).
JPL Horizons output is queried, never committed, so a fresh clone has none,
and `check` reports two tests skipped -- `horizons_fixture_checksums` and "the
generated Sun fixture reads, in metres" -- which is correct and loud, but means
nothing has been checked against the data. The recipe is
[`../data/horizons/README.md`](../data/horizons/README.md): one `curl` and one
`python3 scripts/horizons-fixture.py`, run from `data/horizons/`, then
`sha256sum -c checksums.sha256` must say `OK`. It must before M1-08, whose
budgets are asserted against that file. Behind a proxy that cuts off large
downloads, the README gives the `curl` flags that resume.

**The second toolchain, which is not optional before a milestone lands.** It
has already caught two defects that Windows clang could not see -- an unstable
solver and a missing `<tuple>` include -- so set it up on any machine that will
be doing real work:

```
wsl --install -d Ubuntu --no-launch
wsl -d Ubuntu -u root -- apt-get update
wsl -d Ubuntu -u root -- apt-get install -y build-essential cmake ninja-build g++-14 git
```

**Do not install Ubuntu's `clang` meta-package.** It tracks whatever the
release ships (21.1.8 on 26.04), and this project wants one clang version
across both operating systems, not two. Take it from LLVM's own archive
instead, matching the major version installed on Windows:

```
codename=$(lsb_release -cs)          # "resolute" on 26.04
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
  | gpg --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg
cat > /etc/apt/sources.list.d/llvm-23.list <<EOF
deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] \
http://apt.llvm.org/$codename/ llvm-toolchain-$codename-23 main
EOF
apt-get update
apt-get install -y clang-23 clang-tidy-23 clang-format-23 clangd-23 \
                   lld-23 llvm-23 libclang-rt-23-dev
apt-get remove -y clang-21 clang-tools-21 llvm-21 && apt-get autoremove -y
```

`libclang-rt-23-dev` is not optional: it carries the ASan, UBSan and libFuzzer
runtimes, which is the entire reason a Linux box exists here. The presets ask
for bare `clang` / `clang++`, so point those at 23 with one alternatives entry
that slaves the rest to it:

```
u=/usr/bin
update-alternatives --install $u/clang clang $u/clang-23 100 \
  --slave $u/clang++       clang++       $u/clang++-23 \
  --slave $u/clang-tidy    clang-tidy    $u/clang-tidy-23 \
  --slave $u/clang-format  clang-format  $u/clang-format-23 \
  --slave $u/clangd        clangd        $u/clangd-23 \
  --slave $u/llvm-cov      llvm-cov      $u/llvm-cov-23 \
  --slave $u/llvm-profdata llvm-profdata $u/llvm-profdata-23
```

Then, from inside the distribution, in the repository:

```
cmake --preset linux-sanitize && cmake --build build/linux-sanitize   # ASan + UBSan
cmake --preset linux-gcc      && cmake --build build/linux-gcc        # the second compiler
cmake --preset linux-fuzz     && cmake --build build/linux-fuzz       # libFuzzer, for leaks
ctest --test-dir build/linux-sanitize --output-on-failure
ctest --test-dir build/linux-gcc      --output-on-failure
./build/linux-fuzz/fuzz_orbit -max_total_time=240
```

Fuzzing itself moved to Windows on 2026-09-12 (`windows-fuzz`, VERIFICATION.md
rule 13). The Linux build above is kept only for LeakSanitizer, which has no
Windows equivalent.

`llvm` is there for `llvm-cov` and `llvm-profdata`; `VERIFICATION.md` rule 18
has the coverage invocation. The `--no-launch` matters: it skips the
interactive account setup, so commands run as `-u root` and nothing blocks.

### The third implementation: MSVC, added 2026-09-14

gcc-14 is the second *implementation* and MSVC is the third, added on the day
the machine moved to Visual Studio 18. It builds **the whole tree, renderer
included**, which neither Linux preset does, so it is the only place our Vulkan
and SDL code meets a second compiler at all.

**It needs the MSVC environment, and the preset deliberately does not supply
it.** `cl.exe` reads `INCLUDE` and `LIB` rather than finding the toolchain for
itself, so the preset would have to hard-code an install path — which is
exactly the kind of machine specific that belongs in this file and not in a
file every clone shares. Run it from a Developer Command Prompt, or:

```
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset windows-msvc && cmake --build build/windows-msvc
ctest --test-dir build/windows-msvc --output-on-failure
```

The path above is this machine's; `vswhere -latest -property installationPath`
finds it on another. Note that clang picks the same toolchain up on its own
through the COM setup API — which is why a Visual Studio Installer update
mid-session on 2026-09-13 left clang unable to find any standard library at all
while `vswhere` still answered correctly. If clang suddenly cannot find
`<cassert>`, that is the cause, and it is not something the repository did.

**It earned its keep on its first run**, which is the argument for a third
implementation in one line: `std::isfinite` is not `constexpr` before C++26,
clang and libstdc++ both accept it in a constant expression as an extension,
and MSVC does not. `core/Time.hpp` used it inside a `constexpr` validator, so
`kJ2000` and `kUnixEpoch` were not constant expressions under MSVC and the two
`static_assert`s reading them failed — eight errors from one assumption, in
code that had been clean under two compilers and 642,199 assertions. The
project already had the answer: a `constexpr isFinite`, written for this exact
reason, which lived in `core/DoubleDouble.hpp` where `Time.hpp` could not reach
it. It is in `core/Scalar.hpp` now.

### CLion: use the presets, not its own profile

**There is one build directory, `build/`, with a subdirectory per flavour**, and
CLion shares it with the command line rather than keeping its own. Set up on
2026-09-15; it is two ticks, and they are per-machine because `.idea/` is
gitignored.

CLion 2026.2 reads `CMakePresets.json` by itself and offers every preset whose
`condition` matches the host -- so the six Windows ones appear and the three
`linux-*` ones correctly do not. What it *also* does is enable a stock profile
of its own called `Debug`, which generates into `cmake-build-debug/`. That is
where the second set of trees came from.

In **Settings -> Build, Execution, Deployment -> CMake**: untick `Debug`, tick
`debug` and `relwithdebinfo`. Then delete `cmake-build-debug/` once -- nothing
writes to it again.

Only those two are ticked, deliberately: they are the definition of done, and
CLion reloads *every* enabled profile whenever a `CMakeLists.txt` changes, so
enabling all six would make each edit six configures long. `asan`,
`windows-fuzz` and `windows-msvc` are before-a-milestone tools and are better
run from a terminal. `windows-msvc` in particular would need a **Visual Studio**
toolchain in CLion to supply `INCLUDE` and `LIB`, which is the only toolchain
this project would ever need CLion to define.

The sixth is **`release`** — plain clang Release, no sanitizer and no live
assertions — and nothing in this project's process uses it: the definition of
done is `debug` and `relwithdebinfo`, and decision 5's benchmarks are
RelWithDebInfo too. It is there for a shipping build and has no build tree on
this machine. *(Named here 2026-09-21. This paragraph accounted for five of
the six, and `release` appeared in no document at all — which is how a preset
that nobody has run since it was written stays that way without anyone
deciding it should.)*

**No custom toolchain is required for the other five**, which was checked rather
than assumed: CLion had already resolved clang 23.1.0 and Ninja with no
`toolchains.xml` at all, and its `cmake-build-debug/CMakeCache.txt` named the
same `C:/GitHub/clang+llvm-23.1.0-.../clang++.exe` our presets do. The duplicate
directory was never a second toolchain, only a second copy.

One rule now that the trees are shared: **do not build from CLion and a terminal
into the same tree at once.** Ninja takes no lock, and two builds interleaving in
one directory is a bad afternoon.

**Nothing about this project lives outside the repository.** Working
agreements are in `CLAUDE.md`, decisions in `docs/adr/`, verification practice
in `docs/VERIFICATION.md`, and this file holds the state and the machine
specifics. The only things a fresh clone lacks are build output and the Blue
Marble imagery under `data/textures/`, and
[`data/textures/README.md`](../data/textures/README.md) has the URLs for that.

---

## 3 and 4: the change history

**Moved to [`HISTORY.md`](HISTORY.md) on 2026-09-09**, verbatim and with its
section numbers kept, so a reference to "`PROJECT_STATE.md` section 3" still
finds the same text. Section 3 is the sessions in order — the review-and-fix
session of 2026-09-05, the realism and verification session, the clang 23
upgrade, and M1-01. Section 4 is `propagate()` failing at 1 AU and the two
numbers without units behind it.

Nothing there is needed to perform a task; it is there for when you need to
know why something is the way it is.

---

## 5. Decisions taken, and where they are written down

**The list of ADRs is in [`STATUS.md`](STATUS.md).** Read the relevant one
before changing anything it covers.

Two documents were added on 2026-09-06 alongside ADR 0006 and are binding:
[`plan/realism.md`](plan/realism.md), the gap list between what exists and what
that decision requires, ordered by structural risk; and
[`VERIFICATION.md`](VERIFICATION.md), the rules for keeping bugs out, whose
Part 4 records which of them a machine currently checks and which do not yet
exist. `CLAUDE.md` points at both.

**Milestone 1 has two documents of its own, and both are load-bearing.**
[`plan/milestone-1-tasks.md`](plan/milestone-1-tasks.md) is the queue: 87 tasks
in one order, each with its own document under `plan/tasks/`, and the standing
rules every task inherits.
[`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) is the
register: the twenty-six rulings the queue was built on, the error budgets, the
scope fences, and a table mapping each decision to the ADR that records it.
Read the register before proposing anything the queue seems to have missed --
section 6 exists so that a deliberate omission is not mistaken for an oversight.

Third-party licences left the register on 2026-09-09 and now live in
[`../THIRD_PARTY.md`](../THIRD_PARTY.md), including which files of
`bc7enc_rdo` may be compiled and which may not.

---

## 6. Decisions that were reviewed, and how they landed

I made these on my own during the session. They were put to the project owner
on 2026-09-06; the rulings are recorded here so nobody re-opens them by
accident.

### 6.1 Five clang-tidy checks — RULED: re-enable all five, fix the code

Every check I had suppressed or narrowed is back on. Both `.clang-tidy` files
now carry exactly the suppression list they had before this session. The code
absorbed the findings instead:

| Check | How it was satisfied |
|---|---|
| `portability-avoid-pragma-once` | All 10 project headers and all 7 example headers converted to include guards (`ORBSIM_CORE_SCALAR_HPP`, `ORBEX_CORE_VEC3_HPP`, —) |
| `readability-redundant-member-init` | The `{}` dropped from `Elements` and `OrbitInfo` (project) and `Elements` (example). Safe because every member is a unit type and `Quantity` supplies its own default member initializer. `bool closed{}` keeps its `{}` — that one genuinely needs it |
| `performance-enum-size` | The example's four enums take an explicit `std::uint8_t` base, justified as interface rather than optimisation. It was still suppressed in the root project at the time; **that ended on 2026-09-08**, when the whole list was emptied and five enums here were given an explicit base too |
| `misc-non-private-member-variables-in-classes` | `NOLINT` on `Vec3`, `Quat` and `Quantity::value`, each with the reason on the line. It was still suppressed in the example at the time; **that ended on 2026-09-08** as well. Neither `.clang-tidy` suppresses either check now — both lists hold exactly the same four entries |
| `cppcoreguidelines-macro-usage` | `NOLINTNEXTLINE` on the two assertion macro definitions in each project |

The principle the owner set, worth keeping: **fix the code; suppress only when
the code cannot be fixed, and then at the site, not in the config.** A
suppression in `.clang-tidy` silently covers whatever is written next; one on
the line covers only that line.

There were four suppression sites in the project when this was written (two
macros, the value types, one commutative-parameter pair, one seeded RNG) and
four in the example. **Counted on 2026-09-13 there are thirteen `NOLINT` lines
across five files in `src/` and twenty-one in `tests/`** — the growth is the
2026-09-08 ruling working as intended, since emptying the config list moved
suppressions to the sites that actually need them: three `SDL_Log` varargs, two
`reinterpret_cast`s the C API requires, the seeded generators, and the
Catch2-macro complexity scores on individual test cases. Every one carries its
reason, which is the property that matters; the count is not a number to keep
down for its own sake.

**Recounted 2026-09-21: 32 directives across 11 files in `src/`, 85 across 13
in `tests/`, and 4 in the worked example** — counting `NOLINT(`,
`NOLINTNEXTLINE(`, `NOLINTBEGIN(` and `NOLINTEND(` over tracked files only.
Nine of the 22 that register decision 96 turned up on 2026-09-20 are in that
total, each on a provably symmetric parameter pair with the reason at the site.
The growth is still the 2026-09-08 ruling working as intended and not a
regression, but a number written down once and left for eight days of task
work is worth re-reading before it is quoted: **if you need the current
figure, run the count rather than reading either of these.**

### 6.2 `EXAMPLE.md` was deleted — RULED: leave it deleted

923 lines, the prose predecessor of `coding-guidelines-example/`. Nothing
linked to it and it had drifted to a different namespace (`orb::` vs the
directory's `orbex::`). Recoverable with
`git checkout 1cae874^ -- EXAMPLE.md` should that ever be wanted.

### 6.3 The `asan` preset is RelWithDebInfo, not Debug — still my call

ASan and the MSVC *debug* C runtime cannot share a heap: every sanitized
executable died at startup with a bad-free inside `ucrtbased.dll` before
reaching `main`. The preset builds RelWithDebInfo with `-DNDEBUG` removed, so
assertions are live but the release runtime is used. Verified: all four suites
pass under ASan and `NDEBUG` appears nowhere in the generated build.

Two Windows-specific workarounds ride along.
`_DISABLE_STRING_ANNOTATION` / `_DISABLE_VECTOR_ANNOTATION` are needed because
vk-bootstrap is not instrumented and the MSVC STL refuses to link mixed
annotations; the cost is overflow detection *inside* `std::string` and
`std::vector` spare capacity, with heap, stack and use-after-free intact. And
`clang_rt.asan_dynamic-x86_64.dll` is copied into the build tree, because
clang's ASan runtime is not on `PATH` and its absence kills every sanitized
executable with `STATUS_DLL_NOT_FOUND` before it prints anything.

The `linux-sanitize` and `linux-gcc` presets **have now been run, and both pass**
(2026-09-07). WSL 2 turned out to be enabled already with no distribution
installed, so the Linux box was one `wsl --install -d Ubuntu` away. Each preset
reports 3,632 checks and zero failures, matching Windows exactly, and UBSan was
confirmed genuinely active rather than merely configured. Nothing runs them
automatically — with CI declined they are a deliberate act before a milestone
lands. See `VERIFICATION.md` rule 20.

### 6.4 Scope that went beyond "fix the findings" — still unreviewed

The `Quantity` CRTP base, the `core/Scalar.hpp` split, the four new unit types
(`MetresPerSecond`, `RadiansPerSecond`, `SpecificEnergy`, plus `Tolerance`
moving down), and renaming `InitError` to `RenderError` were refactors I chose.
They are covered by ADR 0001 and 0002 and everything passes, but none was
asked for.

---

## 7. Open questions — for the project owner, not for me

1. **Which GPU should the renderer select? Settled: always the discrete one.**
   Decided 2026-09-06 and implemented in `makeDevice`. No flag; a machine
   without a discrete GPU falls back and says so in the log.
2. **CI: settled, and the answer is no.** The owner decided on 2026-09-06 that
   this project does not use continuous integration. Do not add it and do not
   spend time on it. Verification is the `check` target, run locally by a
   person before they call something done.

   ADR 0005 recorded the cost as **"UndefinedBehaviorSanitizer and a second
   compiler are now out of reach"**. That turned out to be false, and it is
   corrected in that ADR's 2026-09-07 update: WSL 2 was already enabled here,
   so `wsl --install -d Ubuntu` bought back both. UBSan's Windows support is
   still partial and `ORBSIM_SANITIZE_UNDEFINED` still refuses to configure
   there; the difference is that "there" is no longer the only option.

   What remains true is that nothing runs them automatically. The
   `linux-sanitize` and `linux-gcc` presets are run by hand, and it is worth
   doing before a milestone lands: a different compiler and standard library
   disagreeing with clang is where a certain class of bug first shows itself.

   `.github/workflows/ci.yml` was added in commit `eb2a99b` and deleted again
   on 2026-09-06. It is recoverable from history if the decision is ever
   revisited; nothing in the tree refers to it.
3. **Tile format on disk: settled, and the answer is KTX2 with BC7.** Decided
   2026-09-08 (register decision 20): it carries the mip chain and the Vulkan
   format enum directly, and it has the cleaner spec. DDS does not disappear —
   Orbiter's `Surf` tiles are DXT1 inside a DDS header, and the converter
   repacks those blocks into KTX2 unchanged rather than transcoding them
   (decision 21).
4. **Elevation and night lights — both settled.** Elevation moves into phase C
   (2026-09-07): ADR 0006 makes relief part of the realism bar, and it is much
   cheaper inside the quadtree than after it. The dataset is ETOPO 2022,
   60 arc-second, ice surface (decision 23). Night lights fold into phase B
   (decision 25); the specular water mask stays deferred until a source is
   located.
5. **The physics fidelity question is settled: a simulation, not a sandbox.**
   ADR 0006, decided 2026-09-06. What remains open from that decision is the
   *technical* fork list in [`plan/realism.md`](plan/realism.md) section 4.
   Three of those were settled on 2026-09-08 — Cowell then Encke, RK4 then a
   high-order tableau, and a Cartesian state — and are recorded as an ADR.
   **Still open: DE440 or VSOP87 for the ephemeris, and how far up the
   spherical-harmonic field to go.** Milestone 1 needs neither: it uses the
   analytic Sun and stops at J2.
6. **Is `Vec3` staying unit-free? Settled 2026-09-17: no.**
   [ADR 0019](adr/0019-vectors-carry-their-unit.md) is **accepted**, both steps
   done, in two commits. `core/Units.hpp`'s nine types are built on mp-units
   `v2.5.0` and `Vec3<R>` is templated on an mp-units reference, so
   `Metres / Seconds` is a `MetresPerSecond`, `cross(r, v)` is m^2/s with no
   named type needed, `Eccentricity` is a *kind* no other ratio converts into,
   and `Position + Velocity` does not compile.

   The thing that nearly stopped step 2 is worth remembering: **mp-units has no
   `vector_product` on quantities**, in `v2.5.0` or on master 102 commits later
   -- the overload its own blog post shows is a commented-out TODO. Templating
   `Vec3` on the *reference* rather than on a quantity sidesteps it entirely,
   because the reference algebra (`R1 * R2`) is supported and it is only the
   vector operations that are ours. The record supersedes
   [ADR 0001](adr/0001-units-in-the-type-system.md) on the mechanism and closes
   rule 17 of [`VERIFICATION.md`](VERIFICATION.md) with it.

   Four options are costed there; the recommendation is a full compile-time
   dimension system, in two steps, **after M1-04**. Measured while writing it:
   `Vec3` appears 104 times in `src/` and `tests/` and **zero times in
   `src/render/` or `src/app/`**, so this is the cheapest it will ever be --
   phase A adds a camera, phase C a quadtree, phase F the first narrowing of a
   position to `f32`. Read the record before agreeing or disagreeing; it also
   says what it deliberately does not decide, which is frames.

7. **MSVC warnings: settled 2026-09-17, `/Wall`.** ADR 0017's rule applied to
   the third compiler as to the other two. The folklore that `/Wall` is
   unusable was measured rather than repeated: over our own translation units
   -- dependencies are SYSTEM and unjudged -- it produced warnings in **eight
   codes**, all noise or already-decided policy, and every one is switched off
   in `CMakeLists.txt` with its count and its reason beside it. `/W4` is gone.
   The eight: C4514 (unreferenced inline removed), C4820 (padding -- the same
   diagnostic as clang's `-Wpadded`, switched off on 2026-09-11 for reasons that
   did not change), C4623/4625/4626/5026/5027 (implicitly deleted special
   members, which is Rule of Zero working), C4868 (MSVC declining to promise the
   left-to-right initializer order C++17 already requires) and C5045 (a remark
   about `/Qspectre`, which is not passed). Nine sites could drop the
   deleted-special-member group by writing `= delete` explicitly; that trades the
   rule for the boilerplate the rule exists to avoid, and was not done.

8. **Horizons output: settled 2026-09-17, do not commit it.** The terms were
   read that day and closed nothing — no licence is stated anywhere, and the SSD
   FAQ asks to be told what you intend to use and how
   ([`../THIRD_PARTY.md`](../THIRD_PARTY.md) has the four sources). The owner
   ruled: **query Horizons, do not redistribute its output.** Fixtures are
   generated into gitignored `data/horizons/`; the recipe is committed, with
   every API parameter and its reason, and so are the fixtures' SHA-256 sums, so
   a regenerated file can be verified as the one the budgets were measured
   against. M1-06 is amended.

   The cost is recorded rather than glossed: the external-truth suites cannot
   run on a fresh clone until somebody runs the recipe, so they must report
   themselves **skipped and say so loudly** — a silent pass would be exactly
   ADR 0005's "a step that has silently been doing nothing".

   **Corrected 2026-09-19, by measurement.** The ruling assumed a regenerated
   file could be checked against a committed hash, and a raw Horizons response
   never can be: line 7 stamps the moment of the request, and two more lines
   change daily. So each response is converted, deterministically, by
   `scripts/horizons-fixture.py`, and the hash is of the converted file -- two
   fetches at different moments converted to byte-identical files. The ruling
   stands; the mechanism under it changed. See item 13.

9. **`quickTwoSum`'s precondition: settled 2026-09-17, the operators use
   `twoSum`.** Found by asserting it, which is what an assertion is for. The
   compiler named the case during constant evaluation:
   `quickTwoSum(0.0, 8.673617e-19)`, reached from `(1 + 2^-60) - 1`, where
   `|a| = 0` is smaller than `|b| = 2^-60`. It happened whenever the high parts
   cancelled -- `operator+` renormalised with `quickTwoSum(sum.hi, ...)` and
   after cancellation `sum.hi` can be tiny or zero while the low terms are not.
   `quickTwoSum` is exact only inside its precondition.

   The three renormalisations use `twoSum` now: six operations rather than
   three, correct for any pair. **The speed turned out not to be at stake** --
   measured over the two heaviest suites, three runs each, the difference is
   inside the noise, and every assertion including the cross-toolchain checksum
   is unmoved. `quickTwoSum` is kept, its precondition now asserted, and
   exercised by its own test, so the next caller gets the check rather than the
   belief. Its test's comment used to claim the operators satisfied the
   precondition by construction; that claim is what this disproved, and the
   comment says so now.

10. **The two `assign*` out-parameters in `elementsFromState`: settled, they
    stay.** `assignInPlaneAngles` and `assignConic` take an in/out `Elements&`.
    Converting them to return their results was tried on 2026-09-17 and reverted:
    it is a clean change in itself, but it costs six lines at the call site and
    pushes `elementsFromState` past `readability-function-size`. They violate no
    stated rule -- they are not a `bool` plus an out-parameter, and their two
    parameters cannot transpose -- and the budget is a better reason for their
    shape than the aesthetic one previously recorded. Reopen only with a plan for
    the caller's size.

---

11. **mp-units' own contract checking: settled 2026-09-18, leave it off.**
    `MP_UNITS_API_CONTRACTS` is set to `NONE` in both this project's
    `CMakeLists.txt` and the worked example's, which compiles the library's 60
    assertions to nothing. Measured that day before deciding, because the
    question deserved a number rather than a preference:

    - **54 of the 60 are compile-time**: prime factorisation of unit magnitudes
      (16), unit symbol text (10), fixed strings (10), rational arithmetic on
      magnitudes (8), and a few singles. A violated `constexpr` precondition is
      a compile error whether or not contracts are on, so enabling them buys
      nothing there. Five more are in text formatting, which this project never
      does to a quantity.
    - **Six are runtime**, all in `framework/quantity.h`, and all six are the
      same check: division or modulo by zero. Of those, `Scalar<R1> / Scalar<R2>`
      is *our* operator working on raw values, so mp-units' checked divide is
      never even called; only "quantity over a bare double" routes through it,
      and the one place that matters -- `normalize()` -- already guards.
    - **What it would add is the category [ADR 0002](adr/0002-error-handling-strategy.md)
      forbids.** Division by zero here is caller-caused: fuzzer states, scenario
      files, user input. The rule is that those are *reported* through
      `std::expected` and only what a bug could produce is asserted. gsl-lite
      terminates or throws; neither is `return std::unexpected(...)`. This
      project has already been bitten by exactly that shape, and the comment is
      still in the code at `src/orbit/Orbit.cpp`: `ORBSIM_ENSURES(rMag > 0.0)`
      aborted a Debug build on user input, a fuzzer found it, and it became a
      reported `NotFinite`. A library assert cannot be fixed at the site the way
      that one was.
    - **Neither GSL is fetched for us.** Both options do
      `find_package(... REQUIRED)`, so switching would mean adding and pinning
      gsl-lite or Microsoft.GSL ourselves, in both projects.
    - **The two are not equivalent, if this is ever revisited.** Under `MS-GSL`
      the six runtime checks sit behind `#if defined NDEBUG`, so they exist only
      in Debug and asan; under `GSL-LITE` the gating is gsl-lite's own business.
      Routing them to `core/Contract.hpp` instead is *not* available:
      `compat_macros.h` defines the four macros unguarded, with no `#ifndef`, so
      it would mean patching a dependency and this project vendors nothing.

    **The cheap way to reopen it** is not argument but measurement: turn
    `GSL-LITE` on in a throwaway tree, run the suites and the fuzzer, and see
    whether any contract fires. Nothing fires means the guards are complete;
    something fires means a real gap, to be fixed by reporting rather than by
    shipping an abort.

12. **M1-04's ten questions: all settled 2026-09-18, and all before any code
    was written.** They are decisions 32-41 of
    [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9,
    and the substantive ones are summarised in
    [ADR 0009](adr/0009-time-is-a-type-with-a-scale.md)'s update of that date.
    The four worth knowing without opening either:

    - **The leap-second table expires 2027-01-01T00:00:00 UTC**, which is what
      IERS Bulletin C 72 guarantees and nothing more. The IERS
      `leap-seconds.list` claims 2027-06-28, which additionally assumes the
      March 2027 opportunity goes unused. `docs/STATUS.md` carries the date, and
      **no test fails on a calendar date** -- a time bomb in the suite would
      trade one silent failure for another.
    - **A UTC Julian date is ERFA's quasi-Julian date**: the fraction is of that
      UTC day, whatever its length. Anything handing a UTC instant to ERFA in
      M1-05 or M1-07 wants that convention, not a fraction of 86 400 s.
    - **`detail::Builder` is the one door** into `TimePoint`'s private
      constructor, and the only thing it befriends. M1-05's TDB and UT1
      conversions go through it rather than adding friends of their own.
    - **`ttFromTai` and `taiFromTt` do not return `std::expected`**, because
      they cannot fail. Only the four conversions that consult the table report.

    What is *not* settled by any of this: whether a negative leap second is ever
    announced. The arithmetic handles one, a `static_assert` records that every
    published step so far is +1, and the only thing that exercises the negative
    path is a synthetic table in the suite. If one is announced, that
    `static_assert` is the reminder to re-read the tests that assume otherwise.

13. **M1-06's questions: all settled 2026-09-19**, decisions 42-52 of
    [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9.
    Nine were put before the code; the owner's first answer on the format was
    A, and it was confirmed as B before anything was built, because every
    answer the round before had been A. Two more came from the linter mid-task
    and were put the same day, each with a measurement. What to know without
    opening the register:

    - **Reference data arrives through `tests/FixtureFile.hpp`**, in this
      project's own plain format. The untyped layer keeps every field as text;
      a typed reader turns it into units at once. `readStateVectors()` is the
      first, and gives `TdbTime`, `Position` and `Velocity`, with kilometres
      becoming metres by moving the decimal exponent rather than multiplying.
      M1-05's TDB values and M1-68's GMAT trajectory are to use the same format
      and add typed readers of their own.
    - **Horizons responses are converted, never used raw**, by
      `scripts/horizons-fixture.py`, which refuses a response in the wrong
      units, frame or corrections. A new Horizons fixture is a curl, a
      conversion, and a line in `data/horizons/checksums.sha256`.
    - **`check` verifies the fixtures' hashes** and reports a missing fixture as
      skipped. On a fresh clone that is expected, and loud.
    - **`TimePoint`'s members are initialised** (`core/Time.hpp`), so any struct
      holding an instant passes `cppcoreguidelines-pro-type-member-init`.
    - **gcc's `-Wabi-tag` is off at one site** -- the three untyped structs in
      `tests/FixtureFile.hpp`, which hold `std::string`. Any other type of ours
      holding a `std::string` with external linkage will draw it on gcc again;
      the question to ask then is whether the string is needed at all, as it
      was not for `dataDirectory()`.

14. **M1-05's questions: all settled 2026-09-19**, decisions 53-67 of
    [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9.
    Fourteen were put before the code, each with a measurement behind it; a
    fifteenth was found while the answers were being written down, and put and
    ruled the same day. What to know without opening the register:

    - **ERFA v2.0.1 is pinned** and built by our CMake from exactly 249 files,
      counted at configure time, with its version macros read from its own
      `meson.build` and checked against the pin. Both of its validation
      programs are CTest tests. It links privately to `orbsim_core`, and its
      headers are included in `src/astro/*.cpp` only.
    - **TDB - TT is asserted within 20 us of Skyfield 1.55**, the Circular 179
      series, from a fixture that is committed -- `data/skyfield/`, with its
      generator script and recipe -- so it runs on every clone.
    - **`tdbFromTt` evaluates the series twice**, because its argument is TDB,
      the thing being computed; the round trip is asserted to 1 ps, and exact
      on at least 99% of the sweep -- the rate is what sees the second
      evaluation, where the 1 ps budget cannot (decision 68). Decisions 68 to
      71 came after the mutation pass: two of its three gaps became tests, and
      the geocentre is held by the code.
    - **UT1 needs a `DeltaUt1`**, which is validated and has no default:
      `kDeltaUt1Unmodelled` at the call site is the model error of at most
      0.9 s, written where it is taken. UT1 across a leap second follows ERFA's
      `eraUtcut1`, and the second a negative leap second would remove is
      refused by name, through a table-taking overload the suite drives with a
      synthetic table.
    - **M1-07 inherits a question** (decision 66): past 2027-01-01 a TT clock
      has no route to UT1, because TT reaches UTC only through the leap-second
      table. It is in M1-07's document, to be put with measurements before its
      code.

15. **M1-07's ten questions: all settled 2026-09-19, and three of them became
    a task of their own.** They are decisions 72-83 of
    [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9,
    each with a measurement behind it from a scratch spike. What to know
    without opening the register:

    - **UT1 comes from TT now** (decisions 72-74, and
      [M1-86](plan/tasks/m1-86-ut1-from-tt.md)): UT1 = TT - DeltaT, where
      DeltaT is a validated type the caller names. The leap-second table still
      refuses to invent a UTC label, and the Earth's orientation no longer
      waits on one -- which is what decision 66 left M1-07 to answer. Past the
      table's expiry a caller takes `kDeltaTHeldAtTableExpiry` by name, and the
      model error is the drift of a held value: at worst 1.15 s in a year of
      the IERS EOP 20 C04 series since 1962, 0.54 s since 2000, about +0.1 s a
      year now.
    - **The frame's reference is Skyfield 1.55**, whose route is sidereal time
      applied to the equinox-based matrix where ERFA's is CIO-based, and whose
      nutation is a NOVAS port that shares IERS modules with SOFA's -- recorded
      as the limit of its independence (decision 75). They agree to 54 µas over
      1900-2100, of which 47 µas is s′, the TIO locator ERFA applies even with
      polar motion zero.
    - **The code budget is 0.1 mas over 1900-2100**, a thousandfold tighter
      than the 0.1″ the plan carried, because at 0.1″ the test could not see an
      omitted frame bias, the wrong nutation model, or UT1 wrong by six
      milliseconds (decision 76).
    - **A TT-only rotation is added**, `intermediateFromInertial`, so that
      M1-63's J2 term and M1-08's equinox test can have the pole of date
      without a UT1 (decision 77). At about 28 µs a call, M1-63 must not
      evaluate it at every integrator stage, and its document now says so.
    - **The fuzzer earned its keep again.** M1-86's new claims reached
      `utcFromTt` with an arbitrary TT instant for the first time, and found
      `utcFromTai` asserting a condition a caller can produce. Fixed on
      2026-09-20 in its own commit, ahead of M1-86:
      [`HISTORY.md`](HISTORY.md) has the account.

16. **M1-08's nine questions: all settled 2026-09-20**, decisions 85-91 of
    [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9.
    All nine went up before any code was written, and one of them reversed a
    first instruction of the owner's once the alternatives were costed; a
    seventh ruling, 91, came later, from the linter, while the code was being
    written. Written up here on 2026-09-20, after the fact, because the entry
    was missed when the task landed. What to know without opening the
    register:

    - **The Sun is `eraEpv00`'s, negated**: `src/astro/Sun.hpp` gives the
      geocentric Sun, geometric and in ICRF, reporting
      `OutsideEphemerisRange` past the span ERFA vouches for.
    - **Both budgets were tightened because both were measured first**
      (decision 85): 0.02" of direction and 5e-8 au of distance, where the
      plan carried 0.1" and 1e-6 au, against measured worsts of 0.0085" and
      2.14e-8 au. **The span the budget is asserted over is the fixture's,
      2000-2050**, and that is written into the claim: ERFA's own worst over
      the wider 1900-2100 is 0.016", looser than the budget, so widening the
      span means re-measuring rather than pointing the test at more epochs.
      Recorded honestly: no *named* defect lives between 0.02" and 0.1" --
      the reason to tighten is that a budget with twelvefold headroom absorbs
      a regression in silence.
    - **The calendar year is the wrong window for an apsis** (decision 87),
      and that was found by measuring rather than assumed: perihelion sits
      astride the turn of the year, so a calendar year contains two of them
      and the deeper wins, which puts the "annual minimum" on 31 December in
      2003 and 2047. The sweep runs 1 October to 31 March and 1 April to
      30 September, each window holding exactly one apsis.
    - **The equinox test claims the residual rather than bounding it**
      (decision 86): the declination is asserted within 1e-3 deg of the
      +0.002265 deg the *omitted aberration* predicts, at three USNO
      instants, so a declination of zero fails. The tolerance was proposed at
      2e-4 deg and measured to be wrong before it was written -- the
      published instants are rounded to the minute, and the Earth's monthly
      wobble about the Earth-Moon barycentre reaches about 7e-4 deg.
    - **`TwoPartDate` was deleted, not lifted** (decision 89), reversing the
      owner's first instruction once the pros and cons were put up: it was a
      field-for-field copy of `core/Time.hpp`'s `JulianDate`, so `JulianDate`
      goes to ERFA directly, at `astro/EarthOrientation.cpp`'s call sites as
      well. `astro/Tdb.cpp` is untouched, because it wants ERFA's MJD split
      and builds it inline -- which is the evidence that a shared "the date
      ERFA takes" type would not in fact have been one.
    - **The solar constant is Kopp & Lean (2011)**, 1360.8 +/- 0.5 W/m^2, and
      the code carries 1361 with the rounding justified; the ~0.1% solar-cycle
      variation is **named as unmodelled** rather than left silent, because it
      is a fifth of M1-18's radiometric budget and better known there than
      discovered there (decision 88).
    - **`kAstronomicalUnit` cannot drift from ERFA's**: a `static_assert` in
      `Sun.cpp` holds it bit-identical to `ERFA_DAU`, and `sunDistance` is
      `length(geocentricSunPosition(...))`, one series evaluation rather than
      a second call that could disagree with the first (decision 90).
    - **The mutation pass was twelve of twelve**, three killed by
      `static_assert`s -- and **one kill is recorded for what it actually
      was**: handing `eraEpv00` a date 1.7 ms out, the size of TDB - TT, is
      caught only by the span cases, never by the accuracy budget it appears
      to test, because 1.7 ms of solar motion is 7e-5" against a 0.02" claim.
      A bare "twelve of twelve" would have implied a budget that catches a
      time-scale substitution. It does not.

17. **M1-09's questions: all settled 2026-09-20**, decisions 92-98 of
    [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9.
    Six were put before any code and one, the frames, reversed an earlier
    ruling the same day. What to know without opening the register:

    - **`orbsim_view` exists, and `orbsim_core` cannot link it.** A
      configure-time assertion in `CMakeLists.txt` reads `orbsim_core`'s
      `LINK_LIBRARIES` and fails if it ever appears. It is an INTERFACE
      library until M1-11 adds `Camera.cpp`, because a static library with no
      sources does not configure at all.
    - **`Mat4` carries the units of both spaces *and* the frames** (ADRs 0020
      and 0021). A homogeneous 4x4 has no single unit, so it takes the two
      spaces' references and derives its four block references; the frames
      then unify on the middle of a composition. `core` stays frame-free:
      points are framed by a view-local `FramedVec3`.
    - **The transpose is a dual map**, b* to a*, which is the owner's
      formulation and the reason its composition law holds for a chain
      containing a projection. It depends on `one / (one / metre)` coming back
      as *the same* reference as `metre`, which was measured on all three
      front ends before the design was chosen.
    - **A frame change is declared, once, by `retargetFrame`.** Rotations and
      translations are within a frame. `grep retargetFrame` is the audit and
      M1-11's camera should be its only caller.
    - **Frame-awareness costs nothing at runtime**: 35 instructions against
      35, identical at -O2, measured on the stricter of the two candidate
      designs.
    - **Two tolerances the task document carried were wrong**, and the
      measurements are in the suite's header comment: associativity read
      elementwise is unsatisfiable, and `inverseRigid`'s "identity to 1e-14"
      is dimensionally wrong because the translation column is in metres.

## 8. Gotchas worth not rediscovering

**Six small ones from 2026-09-20, each measured rather than reasoned about.**
They cost an hour between them and would cost it again:

- **`NOLINTNEXTLINE` above a `template` line covers the template line**, not
  the signature, and the finding is reported where the parameters are. Two
  suppressions in that session looked applied and were not.
- **`misc-non-private-member-variables-in-classes` ignores an all-public class
  only while it declares no member *function*.** Confirmed on a three-struct
  probe: the same two public members are reported when `operator==` is a
  defaulted member and silent when it is a free function. That matters for any
  type whose members must stay public because it is used as a non-type
  template parameter, which needs a structural type -- `view/Frame.hpp`'s
  `FrameTag` is the first.
- **`bugprone-easily-swappable-parameters` was blind to most of the tree**
  until that day: `SuppressParametersUsedTogether` defaults to true and
  silences any pair used together in one expression. It is off now;
  `.clang-tidy` carries the counts and what each of the 22 findings became.
- **`clang-format` splits a sixteen-element braced list one value per line**,
  which destroys a matrix literal. `view/Mat4.hpp` builds its matrices from
  `identityMatrix()` and `set()` instead, which reads better than the literal
  did and needs no `// clang-format off`.
- **A deduced `auto` return type defeats NRVO**, and clang's `-Wnrvo` is an
  error here, so a function returning a local names its return type -- an
  alias template where the type is awkward.
- **gcc rejects two things clang accepts on new aggregate code**:
  `-Wctad-maybe-unsupported` on class template argument deduction for a
  template that declares no deduction guide, and `-Wmissing-braces` on a
  singly-braced `std::array`. Both were found by the `linux-gcc` preset after
  Windows was green, which is what the second compiler is for.

**`readability-trailing-comma` reports a comma that is not there.** A call with
two or more arguments, each an empty braced-init of an *aggregate whose members
have default member initializers*, and the check reads the comma **between the
arguments** as a trailing comma inside the braces. Minimal, and confirmed on
clang-tidy 23.1.0 (Windows) and 23.1.1 (WSL) on 2026-09-18:

```cpp
struct A { int m{}; };            // remove the {} and the warning goes away
int f(A, A);
int g() { return f(A{}, A{}); }   // readability-trailing-comma fires on the comma
```

All three ingredients are needed: drop to one argument, drop the default member
initializer, or put anything inside the braces, and it stops firing.

**This is an upstream bug, it is already reported, and it is already fixed --
just not in a release we have.** It is
[llvm/llvm-project#220222](https://github.com/llvm/llvm-project/issues/220222),
"readability-trailing-comma false positive on empty initializer list", closed by
[PR #220548](https://github.com/llvm/llvm-project/pull/220548), **merged into
`main` on 2026-09-04**. There is no backport to `release/23.x`, which is exactly
why 23.1.0 and 23.1.1 both still reproduce it. A second report of the same thing
in a different shape,
[#223646](https://github.com/llvm/llvm-project/issues/223646) (empty braces as a
*default parameter* rather than as an argument), is still open as a duplicate.
**Do not file another one.**

The fix is worth knowing because it confirms the cause: the check now uses the
*written* form of an empty initializer list instead of falling back to the
*filled-in* form, and returns early when it is empty. The filled-in form is what
the compiler produces from `A{}` once the default member initializers are
applied, and it is what was sending the check looking past the braces for a
comma.

`coding-guidelines-example/src/core/Vec3.hpp` works around it with
`std::declval` in the one place it bit -- that example's `Vec3` is an aggregate,
where `src/core/Math.hpp`'s is not, which is why only the example was affected.
Retire the workaround when this project moves to a clang carrying the fix, and
check with the three lines above rather than by trying the old spelling in
context.

**A trait can say yes where the compiler says no.** mp-units gives a
dimensionless quantity an `explicit operator V_()` constrained on V_ being
*constructible* from its representation, while the operator's body needs V_ to
be *convertible* from it. Everything in `core/Units.hpp` is explicitly
constructible from an `f64`, so the operator was viable for all of it and
`std::is_constructible_v<Metres, Eccentricity>` answered **true** while
`Metres{someEccentricity}` refused to compile -- the failure being in the
operator's body, where neither a trait nor a `requires`-expression can see it.
That cost an assertion for a day. `Scalar<>` deletes the operator, in the one
case where mp-units grants it, and the comment there says why. The lesson is
rule 23's: **a check that silently stops checking looks exactly like one that
passes**, so a static_assert about what must not compile is worth occasionally
inverting to watch it fail.

**mp-units leaves a default-constructed quantity indeterminate.** Its storage is
declared without an initialiser and its default constructor is `= default`, so
`Metres m;` holds whatever was on the stack -- where the old hand-rolled
`Quantity<Derived>` had `f64 value{}` and zero-initialised. In a simulation that
claims bit-identical determinism this is not a style point.
`core/Units.hpp`'s `Unit` zeroes explicitly in its default constructor. Found by
`cppcoreguidelines-pro-type-member-init` firing on the seven uninitialised
members of `Elements`, 2026-09-17, which is the tooling doing the job review
would not have.

**MSVC needs `/utf-8` once a dependency's headers contain any.** mp-units names
units with the micro sign, ohm and euro; without the flag MSVC reads a UTF-8
source as the machine's ANSI code page and fails with C3872, *"this character is
not allowed in an identifier"*, pointing into headers that are perfectly
well-formed -- 47 instances of it. clang and gcc assume UTF-8 already, so the
flag makes the three agree rather than adding a behaviour. It is in the MSVC
block of `CMakeLists.txt` with that reason.

**A CRTP base that derives from a third-party type must take `Derived` in its
comparison operators, not itself.** `operator<=>(const Unit&)` as a member makes
every `a < b` ambiguous: mp-units declares its own comparison as a hidden friend
templated on `std::derived_from<quantity>`, which matches `Metres` exactly on
the left, while ours matches exactly on the right, and neither wins. Declaring
ours as hidden friends taking `Derived` on both sides settles it. The same
applies to `operator-`: as a *member* it shadows the base's and
`bugprone-derived-method-shadowing-base-method` reports it, so the arithmetic in
`Unit` is hidden friends throughout.

**An inherited constructor keeps the access it had in the base.**
`using Unit::Unit;` in a derived type does not republish private constructors as
public, so `bugprone-crtp-constructor-accessibility` (which wants the base's
constructors private with `friend Derived`) and a `using`-declaration cannot be
satisfied together. Each of the nine unit types spells its three constructors
out, which is what the pre-mp-units code did anyway.

**MSVC's constant evaluator disagrees with its own runtime about NaN.**
Measured 2026-09-17 on a three-line probe: during constant evaluation MSVC says
`NaN <= max` is **true**; at run time the same expression is false. clang says
false in both, as the standard requires. The consequence is narrow and sharp:
**a `static_assert` about NaN is a claim about the evaluator, not about the
value.** `core/Scalar.hpp`'s `isNaN` is written as
`!(x >= -max) && !(x <= max)`, which is correct everywhere at run time and comes
out false for a NaN under MSVC at compile time -- so the NaN cases are tested in
`tests/test_double_double.cpp` instead, and the header says why. `isFinite`'s
NaN case used to be asserted at compile time and passed under MSVC **by
accident**, because the two wrong answers cancelled; it is at run time now too.

This is also how the assertion arrived: commit 696d767 added those
static_asserts having been verified only in the two clang trees, and broke
`windows-msvc` -- the gate that had been added three days earlier precisely to
catch this class of thing. Run all six before pushing a change to `core/`.

- **clang-tidy header filters need both separators on Windows.** The regex is
  `.*[/\\](src|tests)[/\\].*\.(hpp|h)$`. To check it still matches, run
  `clang-tidy -p build/relwithdebinfo --header-filter='.*' src/orbit/Orbit.cpp`
  and compare the finding count against a plain run.
- **A `Checks:` list drops the defaults**, so `clang-analyzer-*` has to be
  named explicitly. It is the only family that is path-sensitive.
- **A `#` inside `.clang-tidy`'s `Checks:` block is not a comment.** It is a
  YAML folded block scalar, so the `#` and everything after it fold into the
  neighbouring check name -- and the suppression it was attached to silently
  stops applying. `clang-tidy --verify-config` reports it as an unknown check
  and is the only thing that will, which is why the `lint` target now runs it
  first, once per directory whose files are linted. This is also why the
  reasons for the four disabled checks sit in a comment block *above*
  `Checks:` rather than beside the entries.
- **ASan does not work with a Debug build on Windows.** See 6.3.
- **CRLF.** `.gitattributes` normalises the repository to LF and
  `.clang-format` now writes LF, but an older checkout can still hold CRLF in
  files nobody has touched. Those show in `git status` while `git diff` reports
  nothing, because git normalises on read. Leave them alone rather than
  producing a diff in which every line changed.
- **Use CLion's bundled CMake (4.3.1), not the 3.31.2 on PATH.** The build
  tree was configured with the former.
- **`FETCHCONTENT_SOURCE_DIR_<NAME>` silently overrides the pin.** The two
  Windows trees point the fetched dependencies at a shared source cache under
  `build/_deps-cache/`, and the WSL trees at `build/_deps-cache-linux/` --
  **one clone per platform**, decided 2026-09-09, because a Linux build should
  not compile a working tree that Windows git checked out. It saves re-cloning
  SDL3 into every tree, and it costs this: when `FETCHCONTENT_SOURCE_DIR_x` is
  set, CMake uses that directory exactly as it finds it and **never reads the
  `GIT_TAG` in `CMakeLists.txt`**. Changing a pin therefore does nothing at
  all until the cached clone is checked out at the new tag by hand. Both
  caches hold Catch2 at `v3.16.0`, matching the pin.

  **Three of the eight are not in either cache**, and for those the `GIT_TAG`
  in `CMakeLists.txt` is the thing that actually decides the version -- which
  is the right way round, at the cost of a clone per tree. They are
  **Vulkan-Utility-Libraries** (added 2026-09-16), **mp-units** (2026-09-17)
  and **ERFA** (2026-09-19); the five cached ones are SDL3, vk-bootstrap, VMA,
  Vulkan-Headers and Catch2. Read off the `FETCHCONTENT_SOURCE_DIR_*` entries
  in a tree's `CMakeCache.txt` rather than from here, since the setting is
  per-tree. *(This named only mp-units and ERFA until 2026-09-21;
  Vulkan-Utility-Libraries was pinned the day before mp-units and was never
  added to the list.)* Point the caches at them when the re-cloning becomes
  annoying; until then the pin being live is worth more than the disk.

  **Nor is ERFA** (added 2026-09-19), and it has a safeguard mp-units lacks:
  the configure step reads the version out of ERFA's own `meson.build` and
  stops if it is not the pinned one, so a `FETCHCONTENT_SOURCE_DIR_ERFA`
  pointing at another checkout fails loudly instead of building quietly.

  The setting lives in each `CMakeCache.txt`, not in the repository, so
  deleting a build tree loses it and the next configure downloads afresh --
  harmless, but it means two trees can be built from different sources while
  both report green. If a dependency ever behaves differently between trees,
  check this before anything else.
- **`eraC2t06a` applies the TIO locator s' even with polar motion zero**, so
  it is not `Rz(ERA)` times `eraC2i06a`: measured bit-identical in **0 of
  200,000** epochs on 2026-09-20, differing by s' -- -47 µas a century from
  J2000. Compose the rotation by hand and it will be a few tens of
  microarcseconds from ERFA's for no visible reason. The third row, the pole,
  *is* bit-identical between the two, which is what M1-63 and M1-08 rely on.
- **The Epic Games overlay layer** logs a duplicate-layer warning at every
  Vulkan startup. It is noise, not a problem.
- **There is one clang here now, and it is meant to stay that way.** 23.1.0 on
  Windows, 23.1.1 in WSL -- the same release branch; apt.llvm.org publishes
  branch builds rather than the exact tag. Everything resolves to it on both
  sides, `clangd` and `llvm-cov` included. A standalone `clangd_22.1.0` used to
  sit ahead of the LLVM directory on `PATH`, so the editor parsed this code with
  a different front end from the one compiling it; it was deleted on 2026-09-08.
  If diagnostics ever disagree with the build again, check `clangd --version`
  first.
- **`.clangd` points at `build/relwithdebinfo`.** That tree must exist and be
  current, or the editor's diagnostics are fiction. During the 23.1 switch it
  briefly pointed at a deleted tree and every file in
  `coding-guidelines-example/` appeared to be full of unknown types.
- **A clang upgrade leaves the old build trees pointing at the old clang.**
  `CMakeCache.txt` stores the resolved compiler path, so a tree configured
  before the upgrade keeps building with the previous compiler and reports
  green while proving nothing. Delete and reconfigure the tree; do not trust a
  `check` that ran in a stale one.
- **Two clang-tidy 23 checks are wrong on this code, and one of them writes
  code that does not compile.**
  `readability-redundant-parentheses` flags `(1.0_km).value` -- but
  `1.0_km.value` lexes as a single pp-number, so the suffix swallows `.value`
  and the "fix" does not build. `readability-trailing-comma` reads the argument
  separator after an empty braced initialiser (`f(T{}, "x")`) as that list's
  trailing comma, and its fix-it **deletes the separator**. Both are avoided
  here by naming the value instead. Neither fires under clang-tidy 22. If you
  run `clang-tidy --fix` over this tree, build afterwards and read the diff.
- **Three more, found by M1-03 on 2026-09-10**, each reproduced with the real
  clang-tidy 23.1.0 on a probe rather than taken from the editor, and each
  avoided in `core/Time.hpp` or `tests/test_time.cpp` by writing the code
  differently rather than by a suppression:
  - `readability-redundant-parentheses` again, on a requires-clause.
    `requires(isUniform(Scale))` must keep its parentheses -- a function call
    is not a primary expression -- so the fix does not compile. A named
    concept, `requires UniformScale<Scale>`, needs none.
  - `readability-simplify-boolean-expr` applies De Morgan to
    `!(v > -k && v < k)` and writes `v <= -k || v >= k`, which differs when
    `v` is NaN: the first is true, the second false. Where that negated form
    was guarding a cast to an integer, the fix would have let a NaN through to
    undefined behaviour. Testing finiteness first removes the question.
  - `modernize-avoid-c-style-cast` reports casts nobody wrote. When an
    enumerator is substituted into a template, clang wraps it in a
    `CStyleCastExpr` of its own making -- visible in `-Xclang -ast-dump` under
    a `SubstNonTypeTemplateParmExpr` -- and the check reports it at the
    template parameter's name. Written over the type aliases (`TtTime`) rather
    than over `TimeScale` values, the compile-time checks give it nothing to
    see.
- **gcc and clang used to disagree about a field left out of a designated
  initializer**, and clang-tidy stands in the way of the obvious fix. gcc-14's
  `-Wmissing-field-initializers`, part of `-Wextra`, reports the omitted field
  unless it has a default member initializer of its own; clang files the
  designated case under `-Wmissing-designated-field-initializers`, which the
  build switched off project-wide until 2026-09-11. So a struct built by
  designated initializers that stop early --
  `CalendarDate{.year = 2000, .month = 1, .day = 1}` -- compiled on Windows
  and failed `linux-gcc`. Since ADR 0017 the warning is on everywhere except
  `render/VulkanContext.cpp`, and the two compilers agree: measured on
  2026-09-11, each reports an omitted field without a default member
  initializer and neither reports one with it. The fix is an initializer on
  the field, but `Seconds second{};` is exactly what
  `readability-redundant-member-init` removes, because `Seconds` initialises
  itself; `Seconds second{0.0};` calls a different constructor and satisfies
  both. Found by M1-03, 2026-09-10, on the second compiler's first look at
  the code.
- **`-Wfloat-equal` means different things to the two compilers**, measured
  2026-09-11. clang reports `x == y` and a defaulted comparison over a
  `double` member, and exempts a comparison against any literal it holds
  exactly -- `x == 0.0`, `x == 0.5`. gcc reports every literal comparison and
  `x == y`, and not the defaulted one. Neither reports `<`. gcc builds only
  the core and the tests, so in `src/render/` and `src/app/` an `x == 0.0` is
  seen by nothing. Where an exact-zero test is meant,
  `std::fpclassify(x) == FP_ZERO` says so, and agrees with `x == 0.0` on
  every input (`orbit/Orbit.cpp`).
- **Some warnings exist on one ABI only.** `-Wweak-vtables` fires under the
  Itanium ABI, where a class's vtable is emitted with its key function, and
  never under MSVC's, which has no key functions. A header-only polymorphic
  class therefore builds clean on Windows and fails `linux-sanitize` and
  `linux-fuzz`, which is what the test matchers did (ADR 0017). Run the Linux
  presets after adding a class with virtual functions.
- **A gcc-only warning cannot be named in a pragma clang reads.** clang
  rejects `#pragma GCC diagnostic ignored "-Wabi-tag"` as an unknown warning
  group, which `-Weverything` makes an error, so a gcc-only exemption sits
  inside `#if defined(__GNUC__) && !defined(__clang__)`, as in
  `tests/OrbitTestSupport.hpp`. The reverse holds too: gcc reports every
  `#pragma clang diagnostic` line under `-Wall` (`-Wunknown-pragmas`,
  measured), so in code both compilers build a clang-only exemption sits inside
  `#ifdef __clang__` -- `#ifdef`, because clang-tidy's
  `readability-use-concise-preprocessor-directives` rejects
  `#if defined(__clang__)`. The worked example's tests do this.
- **gcc reports `[[clang::lifetimebound]]` as an ignored attribute**
  (`-Wattributes`) wherever it is written, with its default flags as well --
  measured 2026-09-11 on a parameter and on the implicit object. orbsim's
  sixteen uses are all in the renderer, which gcc does not build. The worked
  example's one use is exempted by a gcc-only pragma at the site, by the
  owner's ruling. gcc-14 also accepts `-Wno-attributes=clang::lifetimebound`,
  which exempts that attribute and nothing else -- a misspelt one is still
  reported -- and which clang rejects; all three measured.
- **`-Weverything` includes `-Wshadow-header`**, so a probe that shadows one
  of our headers from another include directory -- the easy way to compile a
  mutated copy -- fails before it tests anything, and looks like a killed
  mutant. Pass `-Wno-shadow-header` to the probe alone, and compile the
  unmutated copy first to prove the harness.
- **gcc lists its C++-only warnings as `[available in C++, ObjC++]`** when
  asked without a source file, rather than as on or off, so a script that
  takes only `[disabled]` misses exactly those. `scripts/gcc-warnings.py`
  takes both.
- **A mutation only counts if it compiles** -- the same trap as the one above,
  by another road. Under `-Weverything -Werror`, `if (false)` is a
  `-Wunreachable-code` error, so a mutant spelt that way fails to build and
  reads as caught: five of eight did on 2026-09-19 before it was noticed. Use
  a condition the compiler cannot prove false (`== ''`, `&& key.empty()`),
  and report a build failure as an invalid mutant, never as a kill. The same
  trap has a second door, found on M1-05's run: a mutant that stops using a
  variable or a parameter -- dropping `estimate`, ignoring `table` -- is an
  unused-variable error under `-Werror`, and two of the first run's were
  invalid that way. Keep the name in use in an expression the compiler cannot
  fold (`estimate.picosecondOfDay() < 0 ? ... : ...`).
- **A Debug-build assertion on Windows opens a modal dialog and waits.** The
  debug C runtime answers `abort()` with "Microsoft Visual C++ Runtime
  Library", Abort / Retry / Ignore, and a script that runs the test and waits
  for it waits forever -- M1-05's mutation harness sat twenty minutes on one
  before it was noticed. The mutant had been caught; nothing could say so. Run
  such a harness beside a watchdog that ends any `test_*` process whose window
  title contains "Runtime Library", and count its exit as a catch by an
  assertion.
- **A mutation harness restores from a file copy, never `git checkout --`.**
  That reverts every uncommitted change in the file, not only the mutant. On
  2026-09-19 it silently threw away M1-04's `core/Time.hpp` before M1-04 was
  committed; the file was rebuilt by replaying every edit from the session and
  proved faithful only because the assertion count came back identical,
  526,531. Copy the file aside first, and compare it byte for byte after.
- **`std::map`'s move constructor allocates under the MSVC library**, so it may
  throw, and `bugprone-exception-escape` reports every struct that holds one --
  a move is expected not to. A handful of entries is better as a
  `std::vector` searched with `std::ranges::find`, which is what
  `tests/FixtureFile.hpp` does.
- **gcc's `-Wabi-tag` fires on any type of ours with external linkage that
  holds a `std::string` or returns a `std::filesystem::path`**, because
  libstdc++ tags both with its "cxx11" ABI. Tagging our types spreads to every
  function returning them (measured 2026-09-19). Ask first whether the string
  is needed at all -- `dataDirectory()` returns a `string_view` of a literal
  for that reason -- and otherwise the owner's ruling is a gcc-only pragma at
  the site, as in `tests/FixtureFile.hpp`.
- **Git Bash mangles what it hands to `wsl`.** An argument starting `/mnt/c/`
  arrives as `C:/Program Files/Git/mnt/c/...`, and a `$var` inside
  `wsl ... bash -c '...'` arrives empty, so a loop over presets silently runs
  with no preset. Call `wsl` from PowerShell, or put the commands in a script
  and pass its `/mnt/c` path from PowerShell.
- **A Catch2 test named with a comma cannot be selected by name** on the
  command line: the comma separates filters, and "No test cases matched" is
  the result. `ctest -R` escapes it.
- **A summary of a web page is not the page.** On 2026-09-19 a summarising
  fetch of JPL's "Approximate Positions of the Planets" gave the Earth-Moon
  barycentre's semi-major axis rate as 0.00562 au per century; the table says
  0.00000562. The first would move the Earth's orbit by 840,000 km a century,
  which is how it was caught -- and why every constant M1-05 cites was then
  read out of the raw page. Quote a number from the source, not from a tool's
  account of it.

---

## 9. If you are picking this up cold

Read in this order:

1. [`STATUS.md`](STATUS.md) — where things stand, and what is next. Short.
2. [`CLAUDE.md`](../CLAUDE.md) — how to work here, and the definition of done.
3. This file, sections 7 and 8 — what is undecided, and what is known to bite.
4. [`adr/`](adr/) — the decisions that span files, and why.
5. [`plan/milestone-1-earth.md`](plan/milestone-1-earth.md), then
   [`plan/milestone-1-tasks.md`](plan/milestone-1-tasks.md) and its
   [decision register](plan/milestone-1-decisions.md) — the milestone, its task
   queue and the rulings both were built on, then the one task document you are
   actually doing.
6. [`CODING_GUIDELINES.md`](../CODING_GUIDELINES.md) and
   [`VERIFICATION.md`](VERIFICATION.md) when you want the arguments, and
   `coding-guidelines-example/` when you want to see them applied.
7. [`HISTORY.md`](HISTORY.md) only when you need to know why something is the
   way it is.

Then run `check` in both trees. If it is green, the tree is as this file
describes it.
