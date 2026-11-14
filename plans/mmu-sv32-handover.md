# Sv32 handover

## Context

M1 (S-mode privilege and trap plumbing) and M2 (Sv32 translation) are done.
M3 (SBI and S-mode devices) and M4 (Linux image and boot) remain. The design
facts and decisions live in [`mmu-sv32.md`](mmu-sv32.md); this file is the
next-step note.

## Current results

| Environment | Pass | Fail |
| --- | --- | --- |
| `rv32izicntr_zicsr_sv32` (minimal) | 127 | 30 |
| `rv32imazicntr_zicsr_zifencei_sv32` (Linux set) | 161 | 30 |
| `rv32imacbzicntr_zicsr_zifencei_sv32` (maximal) | 220 | 31 |

The `I` suite is 39/39 in all three. The `S` suite is 8/8. The
`Sv_Exceptions` group passes in the A-enabled sets. The `Sm` group passes
except `Sm_Zicntr-00`, `Sm_mcsr_cntr-00` and `Sm_xret-00`.

## Next step: M3

The remaining failures are the `InterruptsS`/`InterruptsSm` group and three `Sm` tests that depend on M3:

- `Sm_xret-00` uses `RVTEST_TSBI_GOTO_SMODE`, which needs the SBI to enter S-mode.
- `Sm_mcsr_cntr-00` writes the memory-mapped `mtime` and reads the `time` CSR, which needs the CLINT.
- `Sm_Zicntr-00` counts the instructions in the ecall path; the emulator counts one more than the reference.

## After that

1. Run the Svade and Sstvala coverages on the three Sv32 environments.

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
