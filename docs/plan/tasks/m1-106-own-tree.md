# M1-106 — Judge each mutant file in its own tree

Phase: A | Status: **done, 2026-10-02**
Prerequisites: M1-103
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 255

## Purpose

**`mutants-due.py` takes one tree, and mutant files belong to two.** `m1-12`
to `m1-17` are judged in `build/debug`, the rest in `build/relwithdebinfo`.
Since M1-103 a build-definition change makes a file due only through its
judges' fingerprints, compared in the tree its pass was recorded in; asked
about the other tree, the script cannot compare them, and the strict rule
lists the file due for any build-definition change. On 2026-10-01, after
M1-105, asking `relwithdebinfo` listed 13 files and asking `debug` 19, where
the files the change could reach were 7: safe, never a file missed, but
costly and confusing.

## What to do

As decided (decision 255):

- **Each file is judged in the tree its pass was recorded in, whatever tree is
  named**, and the output marks a file judged elsewhere: `[in build/debug]`.
- **On the safe side where anything is missing**: a file with no record, and
  every run with made-up changes (`--only-assumed`, how the CTest checks hold
  the rule to this tree), is judged in the named tree, as before; a file whose
  recorded tree is not on this machine is due, and the line says why.
- **Test first**: `judging_tree` decides it, and the self-test was seen failing
  on the recorded tree and on the missing tree before it did.

**Mutants**: `scripts/mutants/m1-106.json`, three of them, one per case of
`judging_tree`. Not a mutant: `main_check`'s use of the chosen tree, which only
a real record exercises -- measured instead, below.

## Done when

- [x] The self-test was seen failing first on the cases that need the change:
      the recorded tree, and the recorded tree that is not here.
- [x] Asked about either tree, the same files are listed due, 2026-10-02: nine
      -- the seven M1-105 reaches, and `m1-92` and `m1-103`, since this task
      changes `mutants-due.py`, an input of their judges -- where before it was
      13 and 19.
- [x] `check` passes in both Windows trees, 320 of 320, 2026-10-02.
- [ ] `scripts/mutants/m1-106.json` passes.
