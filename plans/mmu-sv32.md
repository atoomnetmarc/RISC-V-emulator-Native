# Sv32 MMU Plan

## Goal

Run the Linux kernel with paged virtual memory on the RV32 emulator, using the smallest Sv32 implementation that Linux supports.

## Scope

In scope:

- S-mode and U-mode privilege.
- Sv32 two-level page tables with 4 KiB pages.
- Emulator-hosted SBI.
- S-mode timer and external interrupts.
- A third Buildroot image next to the existing no-MMU images.

Out of scope:

- TLB.
- 4 MiB superpages.
- ASID.
- MPRV.
- PMP.
- Svadu, Svpbmt and Svnapot.
- SMP.
- Hypervisor extension.

## Current state

The emulator core implements M-mode, S-mode and U-mode. `state->privilege` holds 0, 1 or 3.

`satp` is writable with MODE, ASID and PPN. `medeleg` and `mideleg` are writable. The S-mode CSRs `sstatus`, `stvec`, `sscratch`, `sepc`, `scause`, `stval`, `sie`, `sip`, `scounteren` and `senvcfg` exist. `senvcfg` has only FIOM writable; the cache-block, pointer-masking, Zicfilp and Zicfiss fields are read-only zero.

`SRET` and `SFENCE.VMA` are decoded. `ECALL` from S-mode raises cause 9. `mstatus.MPP` holds 0, 1 or 3; `SIE`, `SPIE`, `SPP`, `MPRV`, `SUM`, `MXR`, `TW`, `TSR` and `TVM` are writable.

Fetch and data accesses go through `RiscvEmulatorLoadVirtual()` and `RiscvEmulatorStoreVirtual()`. The wrappers translate when `satp.MODE` is Sv32 and the effective privilege is S-mode or U-mode, split a page-crossing access into two chunks, and call the physical `RiscvEmulatorLoad()` or `RiscvEmulatorStore()`. The effective privilege is `mstatus.MPP` for loads and stores when `mstatus.MPRV=1` and the privilege is M-mode; instruction fetches always use the real privilege. The walker implements Sv32 with 4 KiB pages and 4 MiB megapages, the permission checks, Svade and page faults. The board has no SBI layer. The CLINT sets `mip.MTIP`; the PLIC sets `mip.MEIP`.

The ISA-combination generator emits three Sv32 environments: `RV32IZicntr_Zicsr_Sv32` (minimal), `RV32IMAZicntr_Zicsr_Zifencei_Sv32` (Linux set) and `RV32IMACBZicntr_Zicsr_Zifencei_Sv32` (maximal set). The ACT generator maps the `Sv32` subset to the `U`, `S`, `Sv32`, `Svade` and `Sstvala` UDB extensions, enables the S-mode parameters and the Sstvala reporting parameters, turns on the privileged tests, defines `STANDARD_SM_SUPPORTED` so the framework emits the M-mode trap setup and the interrupt entry points, and defines the CLINT timer addresses so the interrupt tests link. The sail reference config delegates every standard exception, including instruction address misaligned, so the U-mode exception tests run. The container image pins ACT `act4` commit `1c3e9f2a`, which sizes the trap signature region per test chunk: the default is 0, the privileged generator reserves 1500 entries, and assembly tests link with a 128-byte stack. The container applies one temporary patch: the trap handler always uses the instruction-width detection in `tests/env/rvtest_trap_handler.h`. The Sv32 environments exclude the U-mode tests that trap-loop under the boot identity map and `Sv_sv32_satp_access_Smode` through `# act-exclude-extensions` in the `.ini`.

The three Sv32 environments build every ELF. The minimal environment runs 119 pass and 38 fail, the Linux set 143 pass and 48 fail, and the maximal set 202 pass and 49 fail. The `I` suite passes 39 of 39 in all three. The remaining failures are the `InterruptsS`/`InterruptsSm` group (the SBI layer and the S-mode interrupt sources are M3 work), the `Sm` shadow-stack group, the `Sv_Exceptions` group in the Linux and maximal sets, and the `S_scsr_ro-00` cycle-counter test. The `Sv_Exceptions` tests reach their failure-message path, which writes to the physical console address `0x10000000`; under Sv32 that address is unmapped, so the write faults and masks the first unexpected trap.

The `core.yaml` template no longer carries the `S`/`Zicsr`-dependent parameters (`MEDELEG_DELEGATABLE`, `MIDELEG_DELEGATABLE`, the `*_INTR_IMPL` flags). `gen_core.py` emits the M-level interrupt flags when the config has `Zicsr` and the delegation arrays plus the S-level interrupt flags when it has `Sv32`. `rv32i`, `rv32izicsr` and `rv32izicntr_zicsr_sv32` all validate and build the `I` suite.

The DTS uses `mmu-type = "riscv,none"` and M-mode interrupt causes 3, 7 and 11. The Linux image uses `CONFIG_RISCV_M_MODE=y`, `# CONFIG_MMU is not set` and uClibc-ng.

The linker script places `.text.rvmodel` directly after `.text.rvtest`, before `.rodata` and `.data`, so the `R_RISCV_JAL` call to `rvtest_clr_sext_int` stays in range when the signature grows.

## Linux constraints

`CONFIG_MMU` and `CONFIG_NOMMU` are the only modes. There is no plug-in MMU.

On RV32, `CONFIG_MMU` means Sv32. Sv39, Sv48 and Sv57 are RV64-only.

`CONFIG_RISCV_SBI` depends on `!RISCV_M_MODE` and defaults to y.

RV32 uses `pgtable-nopmd.h`; the PMD is folded, so Linux uses 4 KiB leaf PTEs and no 4 MiB superpages.

Linux sets A and D at PTE creation. `arch_has_hw_pte_young()` is true only with Svadu. With Svade, a PTE with A or D clear raises a page fault and Linux sets the bit in its fault handler.

Linux uses `sstatus.SUM` for user access and writes `scounteren` in `head.S`.

The kernel entry expects S-mode with `a0` set to the hart ID, `a1` set to the DTB and `satp` set to Bare.

## Decisions

| #   | Decision                                                                                                                                                           | Rationale                                                                                              |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------ |
| 1   | Real Linux kernel with the minimal Sv32 set: 4 KiB pages and 4 MiB megapages, two-level walk, S-mode and U-mode, SBI, MPRV; no TLB, ASID or PMP                     | Linux supports no lighter MMU than Sv32; the ACT Sv32 tests map code and data with megapages and use MPRV |
| 2   | Emulator-hosted SBI: the emulator handles `ECALL` from S-mode with a direct C call in the decode                                                                   | Smallest code, no second binary, no cross-build and no M-mode trap path; the emulator owns the devices |
| 3   | Core-level `RiscvEmulatorTranslate(state, va, access)` ahead of every fetch, load and store; the board keeps physical `RiscvEmulatorLoad` and `RiscvEmulatorStore` | One translation path, a testable MMU and a board that stays physical                                   |
| 4   | S-mode PLIC and CLINT with `mideleg` bits 1, 5 and 9, and `mip.SEIP` and `mip.STIP`                                                                                | One trap path and one `stvec` handler, which matches the S-mode MMU                                    |
| 5   | A third image next to the existing min and max no-MMU images                                                                                                       | Keeps the no-MMU reference and follows the shared-base pattern                                         |
| 6   | Svade: a page fault when A is clear, or when a store finds D clear                                                                                                 | The ACT Svade coverpoint is the only A/D behaviour the test framework can check; hardware update has no ACT coverpoint |
| 7   | uClibc-ng for the MMU image                                                                                                                                        | Reuses the existing defconfig, patches and rootfs overlay; the smallest diff                           |
| 8   | `scounteren` is a separate CSR with a `0x7` write mask                                                                                                             | The manual requires a separate CSR at `0x106`; only CY, TM and IR are implemented                      |
| 9   | ACT coverpoints and the Linux boot                                                                                                                                 | The ACT coverpoints cover the ISA; the Linux boot is the integration test                              |
| 10  | `static inline` virtual load/store wrappers plus a fetch wrapper; call sites change one function name                                                              | One source copy of page-crossing and fault logic; the compiler chooses inlining; no `noinline`         |
| 11  | A page-crossing access is split into two chunks at the page boundary                                                                                               | Correct, at most two translations, lowest AVR cost                                                     |
| 12  | `stval` holds the faulting virtual address, also for a PTE access fault                                                                                            | The manual requires it (Sstvala)                                                                       |
| 13  | One level loop that treats any level as a leaf; the physical address mask depends on the level                                                                     | The manual defines megapages; one path removes the level-0 special case                                |
| 14  | `gen_core.py` sets `fs_legal_states` and `vs_legal_states` to `ExtContext_Off` when F and V are absent                                                              | The reference config must not declare features the DUT does not have                                   |
| 15  | ACT `act4` commit `1c3e9f2a` with the per-chunk trap signature counts and one temporary trap-handler-width patch                                                    | A global `TRAP_SIGUPD_COUNT` cannot serve both the small interrupt tests and the large medeleg walk; upstream #2505 sizes the region per test chunk                    |
| 16  | Exclude the U-mode tests that trap-loop under the boot identity map and `Sv_sv32_satp_access_Smode` through `# act-exclude-extensions` in the Sv32 envs | The boot identity map is not user accessible; tests that run U-mode code without `begin_sv_test` trap-loop in the reference model. The container applies only the trap-handler patch |
| 17  | Move `.text.rvmodel` directly after `.text.rvtest` in `link.ld`, before `.rodata` and `.data`                                                                       | The signature in `.data` pushes `.text.rvmodel` beyond the `R_RISCV_JAL` range for `InterruptsS-00`; the reorder keeps the call in range without an ACT source patch   |
| 18  | `senvcfg` is a read/write CSR with only FIOM writable; `sepc`/`mepc` clear the low bits per IALIGN                                                                  | The manual makes FIOM writable when `satp.MODE` is not read-only zero and the unimplemented extension fields read-only zero; with IALIGN=32 the two low bits of `sepc`/`mepc` are always zero |

## The U-mode exclusion

The boot identity map is not user accessible. Tests that run U-mode code without `begin_sv_test` trap-loop in the reference model. The Sv32 environments exclude these tests through `# act-exclude-extensions` in the `.ini`:

- `Exceptions` (the whole suite)
- `Sstvala_s` and `Sstvala_u`
- `U` (the whole suite)
- `S_Zicntr`
- `Sv_sv32_satp_access_Smode`

`Sv_sv32_satp_access_Smode` is excluded because `_t_satp_access` uses `begin_test_chunk` and does not map the U-mode alias. The container applies only the trap-handler patch. The trap-handler patch stays because it fixes a framework bug that affects tests outside the excluded suites.

## Alternatives considered

| Alternative                               | Why it is not chosen                                                                                                                                                        |
| ----------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Full Sv32 with superpages, ASID and TLB   | More code and test surface; Linux on RV32 needs neither superpages nor ASID                                                                                                 |
| PMP or MPU without paging                 | No page faults, no demand paging and no MMU Linux                                                                                                                           |
| Custom one-level MMU                      | Not Linux-compatible                                                                                                                                                        |
| OpenSBI                                   | Extra binary, cross-build and boot stage; it needs a large M-mode CSR surface (PMP, `menvcfg`, `mseccfg`, `mstatush`, `mcountinhibit`) that the emulator does not implement |
| Minimal M-mode SBI stub                   | Extra binary and load stage; the emulator-hosted handler is smaller                                                                                                         |
| M-mode PLIC and CLINT with SBI forwarding | Splits the trap path over two privilege levels                                                                                                                              |
| Polling                                   | The 8250 and virtio drivers need interrupts                                                                                                                                 |
| Replace the no-MMU target                 | Loses the no-MMU reference                                                                                                                                                  |
| Separate branch or repo                   | Duplication and double maintenance                                                                                                                                          |
| Parameterized build matrix                | More Makefile logic; the third image follows the existing pattern                                                                                                           |

## Architecture

### Privilege and CSRs

The privilege state holds 0 (U-mode), 1 (S-mode) and 3 (M-mode). `mstatus.MPP` holds two bits and accepts 0, 1 and 3. `mstatus.SIE`, `SPIE`, `SPP`, `SUM`, `MXR`, `TW`, `TSR` and `TVM` are writable.

`medeleg` and `mideleg` are writable. A synchronous exception goes to S-mode when the matching `medeleg` bit is set and the active privilege is S-mode or U-mode. An interrupt goes to S-mode when the matching `mideleg` bit is set and the active privilege is S-mode or U-mode. All other traps enter M-mode.

The S-mode CSRs are:

| CSR          | Address | Notes                                                                         |
| ------------ | ------- | ----------------------------------------------------------------------------- |
| `sstatus`    | 0x100   | View on `mstatus`; SIE, SPIE, SPP, FS, XS, SUM, MXR, UBE and SD               |
| `sie`        | 0x104   | View on `mie`, masked by `mideleg`                                            |
| `stvec`      | 0x105   | Direct and vectored mode                                                      |
| `scounteren` | 0x106   | Separate 32-bit storage with a `0x7` write mask; Linux writes 0x2 in `head.S` |
| `sscratch`   | 0x140   | Full 32-bit storage                                                           |
| `sepc`       | 0x141   | Low bit reads as zero                                                         |
| `scause`     | 0x142   | Interrupt bit 31 and cause code                                               |
| `stval`      | 0x143   | Fault address or instruction                                                  |
| `sip`        | 0x144   | View on `mip`, masked by `mideleg`                                            |
| `satp`       | 0x180   | MODE bit 31 (WARL: Bare and Sv32), ASID bits 30:22, PPN bits 21:0             |

`mcounteren` is set to 0x7 at boot so S-mode can read `cycle`, `time` and `instret`. `scounteren` is a separate CSR with a `0x7` write mask that controls U-mode counter access.

`SRET` restores `SIE` from `SPIE`, sets `SPIE`, clears `SPP`, sets the privilege from `SPP` and jumps to `sepc`. `SRET` raises an illegal-instruction exception when the active privilege is below S-mode, or when `mstatus.TSR` is set and the active privilege is S-mode.

`SFENCE.VMA` is decoded and treated as a no-op because the implementation has no address-translation cache. It raises an illegal-instruction exception when `mstatus.TVM` is set and the active privilege is S-mode. `satp` raises the same exception under the same condition.

`WFI` in S-mode raises an illegal-instruction exception when `mstatus.TW` is set.

`ECALL` from S-mode calls the SBI handler instead of raising a trap. `ECALL` from U-mode raises cause 8 and follows `medeleg`.

### Sv32 translation

`satp.MODE` selects Bare (0) or Sv32 (1). `satp.PPN` holds the root page table. `satp.ASID` is stored but has no effect because the walker has no address-translation cache.

A virtual address splits into VPN[1] at bits 31:22, VPN[0] at bits 21:12 and the page offset at bits 11:0. The walk starts at `satp.PPN << 12` and reads a 4-byte PTE at `root + VPN[1] * 4`. A non-leaf PTE points to the next table. A leaf PTE at level 0 maps a 4 KiB page. A leaf PTE at level 1 maps a 4 MiB megapage. The physical address is `(PPN << 12) | (VA & ((1 << (12 + 10 * level)) - 1))`. A level-1 leaf with a non-zero PPN[0] is a misaligned megapage and raises a page fault (`supervisor.adoc`, `sv32algorithm` step 5).

The PTE bits are V (0), R (1), W (2), X (3), U (4), G (5), A (6), D (7), RSW (8:9) and PPN (31:10). A PTE with V=0, or with R=0 and W=1, raises a page fault. A leaf PTE with W=1 and R=0 is reserved and raises a page fault. A non-leaf PTE with D, A or U set raises a page fault (`supervisor.adoc`, `sv32algorithm` step 3).

Permission checks use the effective privilege. The effective privilege is `mstatus.MPP` for loads and stores when `mstatus.MPRV=1` and the privilege is M-mode; instruction fetches always use the real privilege.

- Fetch requires X=1.
- Load requires R=1, or X=1 when `mstatus.MXR` is set.
- Store requires W=1.
- U-mode requires U=1.
- S-mode requires U=0, unless `mstatus.SUM` is set for load and store. S-mode fetch always requires U=0.

A failed permission check raises a page fault with cause 12 (instruction), 13 (load) or 15 (store), and `stval` holds the virtual address. A physical address outside RAM or the device map raises an access fault with cause 1, 5 or 7, and `stval` holds the original virtual address.

The walker raises a page fault when A is clear, or when the access is a store and D is clear (Svade). The walker does not write the PTE. The walker does not cache translations.

Every fetch, load and store goes through `RiscvEmulatorLoadVirtual()` or `RiscvEmulatorStoreVirtual()`. The wrapper translates the virtual address, splits a page-crossing access into two chunks, and calls the physical `RiscvEmulatorLoad()` or `RiscvEmulatorStore()`. A failed translation raises the page fault or access fault and the wrapper returns false.

A misaligned access that crosses a page boundary is split per page. The first and last byte are translated separately; the bytes in between use the translation of their page.

### SBI

The SBI handler uses the standard register ABI: `a7` holds the extension ID, `a6` holds the function ID, `a0` to `a5` hold the arguments, and `a0` and `a1` hold the error and return value.

The handler implements:

| Extension  | ID         | Functions                                                                                            |
| ---------- | ---------- | ---------------------------------------------------------------------------------------------------- |
| BASE       | 0x10       | spec version, implementation ID, implementation version, probe extension, mvendorid, marchid, mimpid |
| TIME       | 0x54494D45 | set timer                                                                                            |
| IPI        | 0x735049   | send IPI, a no-op on one hart                                                                        |
| RFENCE     | 0x52464E43 | remote fence.i, remote sfence.vma, remote sfence.vma.asid, all no-ops without a TLB                  |
| SRST       | 0x53525354 | shutdown and cold reboot                                                                             |
| Legacy 0.1 | 0x0 to 0x8 | set timer, console putchar, console getchar, clear IPI, send IPI, remote fences, shutdown            |

`set timer` writes the CLINT compare value and sets `mip.STIP`. `shutdown` sets the stop flag. The legacy console calls write to the 16550 and read from the 16550.

### Interrupts

`mip` and `mie` gain bits 1 (SSIP), 5 (STIP) and 9 (SEIP). `mip.SSIP` is writable by S-mode software. `mip.STIP` and `mip.SEIP` are read-only and are set by the CLINT and PLIC.

`mideleg` accepts bits 1, 5 and 9. `medeleg` accepts the S-mode exception bits: 0 to 8, 12, 13 and 15. Bit 9 (ECALL from S-mode) and bit 11 (ECALL from M-mode) are not delegated.

The interrupt priority is MEI, MSI, MTI, SEI, SSI, STI. A delegated interrupt enters S-mode through `stvec`, `scause`, `sepc` and `sstatus`.

### Boot

The RISC-V manual requires M-mode at reset (`machine.adoc`, `m_mode_priv_at_reset`). The emulator starts the hart in M-mode at `0x80000000`. The ACT tests run in M-mode and the framework's `RVTEST_BOOT_TO_MMODE` sets up the M-mode CSRs and trap handlers.

Linux with `CONFIG_MMU=y` must run in S-mode because `satp` is an S-mode CSR and is active only in S-mode or U-mode (`supervisor.adoc`). M-mode has no address translation. The no-MMU image uses `CONFIG_RISCV_M_MODE=y` and runs in M-mode. The MMU image needs an SBI and an M-mode firmware that switches to S-mode. The SBI and the firmware stub are not implemented now; the ACT tests are the current work.

The DTB describes the CPU with `mmu-type = "riscv,sv32"`. The CLINT uses S-mode interrupt causes 1 and 5. The PLIC uses S-mode interrupt cause 9. The memory node keeps 64 MiB at `0x80000000`.

### Devices

The CLINT sets `mip.STIP` when the timer compare value is reached. The PLIC delivers external interrupts through `mip.SEIP`. The PLIC context and the DTS `interrupts-extended` cells use the S-mode causes.

### Buildroot

The MMU image uses:

- `CONFIG_MMU=y`
- `CONFIG_RISCV_SBI=y`
- `CONFIG_RISCV_M_MODE=n`
- `CONFIG_SMP=n`

The image keeps the existing kernel version, the ext4 disk root and the virtio devices. The libc is uClibc-ng, the same as the no-MMU images. The kernel `Image` loads at `0x80000000`; there is no firmware binary.

## Milestones

M1 (S-mode privilege and trap plumbing) and M2 (Sv32 translation) are done. M3 and M4 remain.

### M3: SBI and S-mode devices

Deliverable: `ECALL` from S-mode calls the SBI handler; the CLINT and PLIC deliver S-mode timer and external interrupts; `mideleg` bits 1, 5 and 9 are active.

- Add the S-mode `ECALL` branch to `RiscvEmulatorOpcodeSystem()` and `RiscvEmulatorHandleSBI()` to `RiscvEmulator-Native/include/RiscvEmulatorImplementationSpecific.h`.
- Add the S-mode interrupt choice to `RiscvEmulatorLoop()` in `RiscvEmulator/include/RiscvEmulator.h`.
- Add the S-mode context to `RiscvEmulator-Devices/include/RiscvEmulatorDevicePlic.h` and set `mip.STIP` in `RiscvEmulator-Devices/include/RiscvEmulatorDeviceClint.h`.
- Add the S-mode interrupt causes to `RiscvEmulator-Native/configs/linux.dts` and `linux-max.dts`.
- Update `RiscvEmulator-Devices/docs/clint.md` and the PLIC context notes.
- Extend `RiscvEmulator-Devices/test/test_plic/test_plic.c` and `test/test_clint/test_clint.c` with the S-mode context.

Validation: a trap trace with `-t`.

### M4: Linux image and boot

Deliverable: the third Buildroot image boots to a login prompt with `make run-mmu`; the trap trace shows page faults and S-mode interrupts at `stvec`.

- Add `rv32i-linux/configs/rv32i_mmu_defconfig` and `configs/kernel_config_mmu`.
- Add `build-mmu`, `disk-mmu`, `run-mmu` and the MMU DTB target to `rv32i-linux/Makefile`.
- Add an MMU environment to `RiscvEmulator-Native/platformio_linux_env.ini`.
- Set `mmu-type = "riscv,sv32"` in the DTS.
- Update `rv32i-linux/README.md` with the third image and the MMU run command.

Validation: Linux boot, login prompt, console and trap trace.

## Validation

- `riscv-arch-test` S-mode coverpoints: `coverpoints/priv/S_coverage.svh`, `coverpoints/priv/Svade_coverage.svh` and `coverpoints/priv/Sstvala_coverage.svh`, run on `RV32IZicntr_Zicsr_Sv32`, `RV32IMAZicntr_Zicsr_Zifencei_Sv32` and `RV32IMACBZicntr_Zicsr_Zifencei_Sv32`.
- Linux boot with `make run-mmu`, a login prompt and a working console.
- A trap trace with `-t` to confirm that page faults and S-mode interrupts arrive at `stvec`.

The ACT coverpoints cover the ISA; the Linux boot is the integration test.

## Open items

1. `Sv_Exceptions` (10 tests, Linux and maximal sets): find the first unexpected trap. The visible symptom is the failure-message path writing to the physical console address `0x10000000`, which is unmapped under Sv32, so the write faults and masks the real failure. Make that path translation-safe.
2. Counter group: `S_scsr_ro-00` reads the cycle counter before and after a write and expects the same value, but the counter increments.
3. `Sm` group: the shadow-stack (`Zicfiss`) and the M-mode CSR tests.
4. M3: the SBI layer and the S-mode interrupt sources (`InterruptsS`/`InterruptsSm`).
5. Run the Svade and Sstvala coverages on the three Sv32 environments.

## References

- RISC-V Instruction Set Manual, Volume II: Privileged Architecture, `src/priv/supervisor.adoc`: Sv32, `satp`, `sstatus`, `stvec`, `sepc`, `scause`, `stval`, `sie`, `sip` and `scounteren`.
- RISC-V Instruction Set Manual, Volume II: Privileged Architecture, `src/priv/machine.adoc`: `mstatus`, `medeleg`, `mideleg`, `mip`, `mie`, `mcounteren` and `menvcfg`.
- Linux kernel source, `arch/riscv/kernel/head.S`: S-mode entry and `scounteren`.
- Linux kernel source, `arch/riscv/include/asm/pgtable-32.h`: Sv32 PTE format and folded PMD.
- Linux kernel source, `arch/riscv/include/asm/pgtable.h`: A/D bits, `SUM` and `MXR`.
- Linux kernel source, `arch/riscv/include/asm/uaccess.h`: `SUM` use.
- Linux kernel source, `arch/riscv/include/asm/sbi.h`: SBI extension and function IDs.
- Linux kernel source, `arch/riscv/Kconfig`: `MMU`, `RISCV_SBI` and `RISCV_M_MODE`.
- `riscv-arch-test`, `coverpoints/priv/S_coverage.svh`, `coverpoints/priv/Svade_coverage.svh` and `coverpoints/priv/Sstvala_coverage.svh`.
