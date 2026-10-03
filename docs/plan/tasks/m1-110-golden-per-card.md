# M1-110 — One golden per graphics card

Phase: A | Status: not started -- its questions ruled 2026-10-03
Prerequisites: M1-17
Decided by: register decisions 287-292; [ADR 0008](../../adr/0008-renderer-verification.md), which this task amends

## Purpose

**A golden image was one file for every machine** (decision 238): measured on
`clear`, whose smooth gradients differ between graphics cards by at most
1/255 after the comparison's 2x2 downsample. That measurement does not cover
what M1-19 draws next. **A line one pixel wide may land on different pixels
on different cards**: Vulkan lets each card choose unless a pipeline asks for
a defined rule, and a line one pixel aside moves a downsampled block far past
the 4/255 cap. Decision 278 measures how far this goes; the owner asked for
per-card goldens regardless, "just to be on the safe side" (decision 287).

So each card is held to pictures approved on that card, and a card with none
fails by name.

## What to implement

- **Goldens under `tests/golden/<vendor>-<device>/<probe>.png`**, keyed by the
  Vulkan vendor and device numbers in lowercase hexadecimal -- `1002-744c` is
  this machine's AMD Radeon RX 7900 XTX -- and not by the driver version: a
  driver update that moves a picture fails and is looked at.
- **A card with no golden for a probe fails, exit 1**, naming the card's
  numbers and the command that approves one -- never a skipped comparison
  (`VERIFICATION.md` rule 23). The probe still writes its five files, so the
  frame can be looked at.
- **The limits are unchanged**: 4/255 largest, under 0.5/255 mean.
- **`clear` moves too** (decision 287) and is approved on each card.
- **ADR 0008 gains an update section** saying that a golden is an approved
  frame *of one card*, why, and what guards against a fault approved on one
  card alone (M1-19's number check, decision 281).

## Questions, and their rulings

Put to the owner on 2026-10-03, before the task starts, and **all four ruled
the same day as recommended** (decisions 289-292):

1. **Who finds the card's folder.** Recommended: the application, through a
   new `--golden-dir <dir>` that resolves `<dir>/<vendor>-<device>/<probe>.png`
   from the device it opened; `--accept-golden` with it writes there, creating
   the folder; `--golden <file>` stays for tests that name an exact file.
   `scripts/check-accept-golden.py` refuses `--accept-golden` beside
   `--golden-dir` as it does beside `--golden`. Alternative: CTest asks the
   application for the card's key first and passes a file path.
2. **The broken-golden tests** (`probe_golden_block`, `_shift`, `_wrong_size`)
   alter `tests/golden/clear.png`, which no longer exists as one file.
   Recommended: alter **the run's own `clear` frame, halved**, which
   `probe_clear` has just written -- the option decision 275 considered -- so
   the tests need no approved golden on the card and still check which limit
   each alteration breaks. Alternative: alter the current card's golden, which
   needs the card's key at test time and fails on a card not yet approved.
3. **Which cards get goldens.** Recommended: the cards `check` runs on -- the
   RTX A2000 and the RX 7900 XTX -- and not the first machine's Intel UHD,
   which runs only with the NVIDIA driver hidden. A card's numbers are written
   in `STATUS.md`'s table for its machine.
4. **The existing `clear.png`**, approved on the RTX A2000 (decision 220).
   Recommended: moved, unchanged, into that card's folder, whose device number
   comes from the sidecar of the owner's run of decision 278's bundle; and a
   new approval of `clear` on the RX 7900 XTX, which the owner looks at here.

## Tests

- The folder name from the vendor and device numbers, in `orbsim_view`,
  tested without a GPU, including the leading zeros of a short number.
- A probe run against a golden directory without the card's folder: exit 1,
  the card's numbers and the approval command on stderr, the five files
  written -- a CTest test in the shape of `probe_golden_missing`.
- `probe_clear` against the card's golden, as today.
- The broken-golden tests as question 2 rules.

## Done when

- [ ] `check` green in both trees, on this machine, with `clear` approved here.
- [ ] **Waits on** decision 278's run on the first machine, for the A2000's
      device number (decision 292).
- [ ] The A2000's `clear.png` in its folder, and `check` green on the first
      machine.
- [ ] ADR 0008's update section, and decision 238's row pointing at 287.
- [ ] `scripts/mutants/m1-110.json` run after the commit and its record
      committed; anchors in older mutant files that this moves, re-pointed.
