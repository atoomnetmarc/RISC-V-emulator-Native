# Sv32 handover

## Context

M1 (S-mode privilege and trap plumbing), M2 (Sv32 translation) and M3 (S-mode devices) are done. M4 (Linux image and boot) remains. The design facts and decisions live in [`mmu-sv32.md`](mmu-sv32.md); this file is the next-step note.

## Current results

| Environment | Pass | Fail |
| --- | --- | --- |
| `rv32izicntr_zicsr_sv32` (minimal) | 85 | 0 |
| `rv32imazicntr_zicsr_zifencei_sv32` (Linux set) | 119 | 0 |
| `rv32imacbzicntr_zicsr_zifencei_sv32` (maximal) | 179 | 0 |

The `I` suite is 39/39 in all three. The `Sv` suite is 36 in the minimal set and grows with the A and C extensions. The `Svade` and `Zicntr` suites pass in all three. The counts are the full set after the U-mode exclusions.

## M3 result

The `InterruptsSm` group exercises interrupt arbitration. The trap signature word 0 vector field is derived from the per-cause stub address, so vector 23 is cause 9 (SEI) and vector 25 or 18 is cause 11 (MEI) or cause 5 (STI).

Two rules from the privileged manual govern the selection:

- A delegated interrupt traps to S-mode only when the active privilege is S-mode with `sstatus.SIE` set, or when the active privilege is less than S-mode. It never traps to M-mode (`supervisor.adoc:383`).
- Interrupts to M-mode take priority over interrupts to S-mode (`machine.adoc:1357`, `supervisor.adoc:396`).

`RiscvEmulatorLoop()` checks the non-delegated interrupts first, then the delegated ones, and uses the raw `mideleg` bit for the target mode. The minimal environment passes 85 of 85.

M3 also has the PLIC M-mode and S-mode contexts, the split `mip.SEIP` software bit and platform signal, and the custom platform CSR pair `0x8C0`/`0x8C1`. The ACT `RVMODEL_SET_SEXT_INT`, `RVMODEL_CLR_SEXT_INT`, `RVMODEL_SET_SEXT_INT_M`, `RVMODEL_CLR_SEXT_INT_M` and `RVMODEL_SET_SSW_INT` macros are injected through `# act-define` in the `.ini` generator. The board has a simple interrupt generator at 0x0C400000. `run-matrix.py` compiles 468 of 468, including the three Sv32 variants.

## Next step: M4

M4 is the Linux image and boot. The deliverables and file list are in [`mmu-sv32.md`](mmu-sv32.md).

The prebuilt OpenSBI 1.9 for `ilp32/generic` boots in the emulator: load `fw_payload.bin` at `0x80000000` with `-b` and a DTB, and the banner prints and the test payload exits 0. The emulator needs the C and Zicntr extensions for that binary, and the RV32 upper-half `mstateen0h`-`mstateen3h` CSRs must be read-only zero.

## After that

1. Run the Svade and Sstvala coverages on the three Sv32 environments.

## Rules

- Run one environment at a time.
- Use the `/grill-me` skill for design decisions.
- Update `plans/mmu-sv32.md` when the code changes.
- No historic references in comments or docs.
- Exclude U-mode tests that trap-loop under the boot identity map; do not raise `trap_sigupd_count` for the trap loop.
- Keep the container patch for the misaligned fetch.
- Keep `.text.rvmodel` directly after `.text.rvtest` in `link.ld`.
- Implement `senvcfg` with FIOM writable and the unimplemented extension fields read-only zero.
