# M1-107 — An upload made visible to what runs after it

Phase: A | Status: **done, 2026-10-03**
Prerequisites: none
Decided by: register decision 272

## Purpose

**`VulkanContext::uploadBuffer` did not do what the Vulkan specification asks
of it, and nothing had ever called it.** It has existed since milestone 0;
M1-18's lambert probes were about to be its first caller, which is how the
gap was found, on 2026-10-03.

Both of its paths fell short:

- **The staging path**, for device-local memory, copied in one submission,
  waited on the fence, and returned; a later draw then reads the buffer. The
  specification's fence signal operation has an **empty second access
  scope**, and the dependency a later queue submission defines covers only
  **earlier host writes** ("Fence Signal Operation" and "Host Write Ordering
  Guarantees" in `chapters/synchronization.adoc` of the Vulkan-Docs
  repository, read on 2026-10-03). So nothing ordered the copy's writes before
  the draw's reads. Khronos's synchronization examples skip the barrier only
  where a semaphore lies between the two submissions.
- **The direct path**, for host-visible memory, wrote with `memcpy` and never
  flushed. That is right only on host-coherent memory, which the allocator
  does not promise.

**Not seen failing on this machine, and not seeable by the validation
layers**, which treat a host fence wait as completing everything before it --
the null result `VERIFICATION.md` rule 23 warns about. The fix rests on the
specification's text, and the owner chose it knowing it cannot be shown
failing here.

## What was done

As decided (decision 272, option A of four):

- **A barrier after the staging copy**, `makeUploadVisibleToLaterCommands`:
  from the copy's writes to every stage's reads and writes, because
  `uploadBuffer` does not know what the buffer is for. At load time the stall
  costs nothing anyone can see.
- **A flush after each host write into mapped memory**, the staging buffer's
  and the direct path's, through `vmaFlushAllocation`, which does nothing on
  host-coherent memory -- the counterpart of `readBack`'s invalidate.

Considered: a barrier for vertex reads only, which every later caller with
another use would have to widen; writing a host-visible vertex buffer in the
probe code and leaving `uploadBuffer` as it was; recording the gap and
changing nothing.

**A separate commit ahead of M1-18's**, as the owner asked, so the history
stays bisectable (`VERIFICATION.md` rule 10). M1-18's work was set aside with
`git stash` while this was made and checked on a clean tree.

**Mutants**: `scripts/mutants/m1-107.json`, three -- the barrier and each
flush taken away -- all declared survivors, since nothing reaches them before
M1-18 and this machine could not show them afterwards. M1-18's pass re-runs
the file with the lambert probes' tests as judges.

## Done when

- [x] `check` passes in both Windows trees.
- [x] The mutation pass recorded, its three survivors declared.
