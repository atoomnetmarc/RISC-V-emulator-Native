# Sv32 handover

## Context

M1 (S-mode privilege and trap plumbing) and M2 (Sv32 translation) are done.
M3 (SBI and S-mode devices) and M4 (Linux image and boot) remain. The design
facts and decisions live in [`mmu-sv32.md`](mmu-sv32.md); this file is the
next-step note.

## Current results

| Environment | Pass | Fail |
| --- | --- | --- |
| `rv32izicntr_zicsr_sv32` (minimal) | 119 | 38 |
| `rv32imazicntr_zicsr_zifencei_sv32` (Linux set) | 143 | 48 |
| `rv32imacbzicntr_zicsr_zifencei_sv32` (maximal) | 202 | 49 |

The `I` suite is 39/39 in all three. The `S` suite is 7/8; only
`S_scsr_ro-00` (cycle counter) fails.

## Next step: the Sv_Exceptions group

The `Sv_ExceptionsZaamo`/`Sv_ExceptionsZalrsc` tests fail only in the
A-enabled sets (the minimal set does not generate them). The log shows the
signature offset mismatch (`0xffff9310` vs `0x14`), the same symptom as an
unexpected trap that aborts the test without restoring `sp`.

The trace shows the test reaches its failure-message path:
`rvmodel_io_write_str` writes each character to the physical console address
`0x10000000`. Under Sv32 that virtual address is unmapped, so the store raises
a store/AMO page fault, the handler treats it as unexpected, and the test ends
with the save-area pointer as the signature pointer. The console write masks
the first unexpected trap, which is the actual test failure.

Plan:

1. Reproduce one test, for example
   `Sv_ExceptionsZaamo_sv32_exceptions_Zaamo_Smode-00`, with `-v 0 -t`.
2. Find the first trap that the handler treats as unexpected, before the
   console write. Compare `scause`, `sepc` and `stval` against the expected
   trap signature.
3. Check the AMO/Zalrsc exception path: misaligned AMO priority, access fault
   versus page fault, and the `stval` value.
4. Make the failure-message path translation-safe (map the console or use a
   physical-address access) so the real failure is visible.

## After that

1. Counter group: `S_scsr_ro-00` reads the cycle counter before and after a
   write and expects the same value, but the counter increments.
2. `Sm` group: the shadow-stack (`Zicfiss`) and the M-mode CSR tests.
3. M3: the SBI layer and the S-mode interrupt sources
   (`InterruptsS`/`InterruptsSm`).
4. Run the Svade and Sstvala coverages on the three Sv32 environments.

## Rules

- Run one environment at a time.
- Use the `/grill-me` skill for design decisions.
- Update `plans/mmu-sv32.md` when the code changes.
- No historic references in comments or docs.
- Exclude U-mode tests that trap-loop under the boot identity map; do not raise
  `trap_sigupd_count` for the trap loop.
- Keep the container patch for the misaligned fetch.
- Keep `.text.rvmodel` directly after `.text.rvtest` in `link.ld`.
- Implement `senvcfg` with FIOM writable and the unimplemented extension fields
  read-only zero.
