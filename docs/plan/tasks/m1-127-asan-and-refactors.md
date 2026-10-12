# M1-127 — The `asan` preset and four unasked-for refactors, reviewed

Phase: B | Status: not started
Prerequisites: M1-129 -- moved up from after M1-124 by the order of register decision 495, ruled 2026-10-12: lower levels first, since its rulings on the `Quantity` base and the `Scalar.hpp` split shape the bottom level of [M1-130](m1-130-code-as-modules.md)'s structure (decision 492)
Decided by: register decisions 449 and 450, ruled 2026-10-10

## Purpose

`docs/PROJECT_STATE.md` sections 6.3 and 6.4 have said "still my call" and
"still unreviewed" since 2026-09-06, while `STATUS.md` says nothing else
stands open. The owner reopened both rather than accept them as they stand.

## What to review

- **6.3, the `asan` preset**: it builds RelWithDebInfo with `-DNDEBUG`
  removed, because AddressSanitizer and the Windows debug C runtime cannot
  share a heap; and it disables the standard library's string and vector
  annotations, because vk-bootstrap is not instrumented. Measured again
  today: whether the runtime clash still holds with the current toolchain,
  and what the annotations cost.
- **6.4, four refactors chosen without being asked**: the `Quantity` base in
  `core/Scalar.hpp`, the split of `core/Scalar.hpp`, the unit types such as
  `MetresPerSecond`, and `InitError` renamed `RenderError`. Each put to the
  owner: keep, change, or undo, with what each choice costs today.

## Done when

- [ ] Each point measured where it can be, and put to the owner.
- [ ] The rulings in the register, and sections 6.3 and 6.4 marked ruled.
- [ ] Any change the rulings ask for made, with `check` passing in both
      trees and, if code changed, the task's mutant file run and recorded.
