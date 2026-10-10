# M1-125 — The documents and comments the review found wrong

Phase: B | Status: not started
Prerequisites: M1-127
Decided by: register decisions 446 and 450, ruled 2026-10-10 on the review's
group 7 ([the findings](../../review/m1-116-findings.md))

## Purpose

**Last of the review's tasks**, because every task before it changes
documents. Statements about the code that are no longer true, counts kept
outside `STATUS.md`, and documents that contradict each other.

## What to do

- **7.1** every row of the review's table, each read again against the code
  as it is after M1-117 to M1-127 -- some will have changed.
- **7.2** every count outside `STATUS.md` removed or pointed there,
  including the stale "96 documents" the review was planned after; the
  router's "the only place" says "toolchain versions and counts".
- **7.3** every contradiction settled in one place: the presets "at every
  phase gate"; **every gate's prerequisites naming the tasks added since**
  (M1-111 to M1-127 in phase B's, M1-114 in phase F's, all of them in the
  milestone's); the phase B gate's licence list pointing at `THIRD_PARTY.md`;
  where the fuzzers run; the register's header and section 8; the finished
  task documents' stale status lines, with a dated note rather than a
  rewrite.
- **`prompts.txt`** gets a one-line header saying what it is (decision 446).
- A search for every stale-count pattern the review used, repeated, to find
  what it missed.

## Done when

- [ ] Every finding of group 7 fixed, and the searches find nothing new.
- [ ] `check` passes in both trees, `doc-links` included.
