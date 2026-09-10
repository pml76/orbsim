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

Needs clang 17+ (23 here), CMake 3.25+, Ninja, and a Vulkan SDK for the loader
and `glslc`. Everything else is fetched and pinned by CMake. Without a Vulkan
SDK, `-DORBSIM_BUILD_APP=OFF` builds the core and its tests.

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
cmake --preset linux-fuzz     && cmake --build build/linux-fuzz       # libFuzzer
ctest --test-dir build/linux-sanitize --output-on-failure
ctest --test-dir build/linux-gcc      --output-on-failure
./build/linux-fuzz/fuzz_orbit -max_total_time=240
```

`llvm` is there for `llvm-cov` and `llvm-profdata`; `VERIFICATION.md` rule 18
has the coverage invocation. The `--no-launch` matters: it skips the
interactive account setup, so commands run as `-u root` and nothing blocks.

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
[`plan/milestone-1-tasks.md`](plan/milestone-1-tasks.md) is the queue: 84 tasks
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
| `performance-enum-size` | The example's four enums take an explicit `std::uint8_t` base, justified as interface rather than optimisation. Still suppressed in the root project, where it was suppressed with a written reason **before** this session |
| `misc-non-private-member-variables-in-classes` | `NOLINT` on `Vec3`, `Quat` and `Quantity::value`, each with the reason on the line. Still suppressed in the example, where it was suppressed before this session |
| `cppcoreguidelines-macro-usage` | `NOLINTNEXTLINE` on the two assertion macro definitions in each project |

The principle the owner set, worth keeping: **fix the code; suppress only when
the code cannot be fixed, and then at the site, not in the config.** A
suppression in `.clang-tidy` silently covers whatever is written next; one on
the line covers only that line.

There are now four suppression sites in the project (two macros, the value
types, one commutative-parameter pair, one seeded RNG) and four in the example.
Every one carries its reason.

### 6.2 `EXAMPLE.md` was deleted — RULED: leave it deleted

923 lines, the prose predecessor of `coding-guidelines-example/`. Nothing
linked to it and it had drifted to a different namespace (`orb::` vs the
directory's `orbex::`). Recoverable with
`git checkout 1cae874^ -- EXAMPLE.md` should that ever be wanted.

### 6.3 The `asan` preset is RelWithDebInfo, not Debug — still my call

ASan and the MSVC *debug* C runtime cannot share a heap: every sanitized
executable died at startup with a bad-free inside `ucrtbased.dll` before
reaching `main`. The preset builds RelWithDebInfo with `-DNDEBUG` removed, so
assertions are live but the release runtime is used. Verified: all three suites
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
6. **Is `Vec3` staying unit-free?** Today a vector's unit is carried by the
   struct holding it (`StateVector::pos` is metres). A `Vec3<Metres>` is
   possible and much more invasive. ADR 0001 records the current answer, and
   ADR 0006 sharpens the question: with several frames in play, a `Vec3` that
   knows it holds barycentric metres would prevent a class of bug that is
   otherwise invisible.

---

## 8. Gotchas worth not rediscovering

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

  The setting lives in each `CMakeCache.txt`, not in the repository, so
  deleting a build tree loses it and the next configure downloads afresh --
  harmless, but it means two trees can be built from different sources while
  both report green. If a dependency ever behaves differently between trees,
  check this before anything else.
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
