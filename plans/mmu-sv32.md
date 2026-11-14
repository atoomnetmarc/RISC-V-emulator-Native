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

`satp` is writable with MODE and PPN; ASID reads as zero. `medeleg` and `mideleg` are writable. The S-mode CSRs `sstatus`, `stvec`, `sscratch`, `sepc`, `scause`, `stval`, `sie`, `sip` and `scounteren` exist.

`SRET` and `SFENCE.VMA` are decoded. `ECALL` from S-mode raises cause 9. `mstatus.MPP` holds 0, 1 or 3; `SIE`, `SPIE`, `SPP`, `SUM`, `MXR`, `TW`, `TSR` and `TVM` are writable.

Fetch and data accesses go through `RiscvEmulatorLoadVirtual()` and `RiscvEmulatorStoreVirtual()`. The wrappers translate when `satp.MODE` is Sv32 and the privilege is S-mode or U-mode, split a page-crossing access into two chunks, and call the physical `RiscvEmulatorLoad()` or `RiscvEmulatorStore()`. The walker implements Sv32 with 4 KiB pages, the permission checks, Svade and page faults. The board has no SBI layer. The CLINT sets `mip.MTIP`; the PLIC sets `mip.MEIP`.

The ISA-combination generator emits three Sv32 environments: `RV32IZicntr_Zicsr_Sv32` (minimal), `RV32IMAZicntr_Zicsr_Zifencei_Sv32` (Linux set) and `RV32IMACBZicntr_Zicsr_Zifencei_Sv32` (maximal set). The ACT generator maps the `Sv32` subset to the `U`, `S`, `Sv32`, `Svade` and `Sstvala` UDB extensions, enables the S-mode parameters and the Sstvala reporting parameters, turns on the privileged tests, defines `STANDARD_SM_SUPPORTED` so the framework emits the M-mode trap setup and the interrupt entry points, and defines the CLINT timer addresses so the interrupt tests link. The sail reference config delegates every standard exception, including instruction address misaligned, so the U-mode exception tests run. The container image raises `TRAP_SIGUPD_COUNT` in `tests/priv/ExceptionsSvZaamo/sv32_exceptions_Zaamo_Umode.S` to 270; the upstream value of 110 overflows the trap signature region in the sail reference model. The interrupt tests fail at run time until M3 adds the SBI layer and the S-mode interrupt sources.

The DTS uses `mmu-type = "riscv,none"` and M-mode interrupt causes 3, 7 and 11. The Linux image uses `CONFIG_RISCV_M_MODE=y`, `# CONFIG_MMU is not set` and uClibc-ng.

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
| 1   | Real Linux kernel with the minimal Sv32 set: 4 KiB pages, two-level walk, S-mode and U-mode, SBI; no TLB, superpages, ASID, MPRV or PMP                            | Linux supports no lighter MMU than Sv32; the gain comes from leaving out optional features             |
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
| `satp`       | 0x180   | MODE bit 31, ASID bits 30:22 read as zero, PPN bits 21:0                      |

`mcounteren` is set to 0x7 at boot so S-mode can read `cycle`, `time` and `instret`. `scounteren` is a separate CSR with a `0x7` write mask that controls U-mode counter access.

`SRET` restores `SIE` from `SPIE`, sets `SPIE`, clears `SPP`, sets the privilege from `SPP` and jumps to `sepc`. `SRET` raises an illegal-instruction exception when the active privilege is below S-mode, or when `mstatus.TSR` is set and the active privilege is S-mode.

`SFENCE.VMA` is decoded and treated as a no-op because the implementation has no address-translation cache. It raises an illegal-instruction exception when `mstatus.TVM` is set and the active privilege is S-mode.

`WFI` in S-mode raises an illegal-instruction exception when `mstatus.TW` is set.

`ECALL` from S-mode calls the SBI handler instead of raising a trap. `ECALL` from U-mode raises cause 8 and follows `medeleg`.

### Sv32 translation

`satp.MODE` selects Bare (0) or Sv32 (1). `satp.PPN` holds the root page table. `satp.ASID` reads as zero.

A virtual address splits into VPN[1] at bits 31:22, VPN[0] at bits 21:12 and the page offset at bits 11:0. The walk starts at `satp.PPN << 12` and reads a 4-byte PTE at `root + VPN[1] * 4`. A non-leaf PTE points to the next table. A leaf PTE at level 0 maps a 4 KiB page. A leaf PTE at level 1 is a 4 MiB superpage and raises a page fault because the implementation does not support superpages.

The PTE bits are V (0), R (1), W (2), X (3), U (4), G (5), A (6), D (7), RSW (8:9) and PPN (31:10). A PTE with V=0, or with R=0 and W=1, raises a page fault. A leaf PTE with W=1 and R=0 is reserved and raises a page fault.

Permission checks:

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

The emulator starts the hart in S-mode at `0x80000000` with `a0` set to the hart ID, `a1` set to the DTB address, `satp` set to Bare, `mcounteren` set to 0x7, and `medeleg` and `mideleg` set to the delegated bits. `mie` and `mip` start at zero.

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

The work is split into four coarse milestones. Each milestone delivers a working slice and has its own validation. The order is M1, M2, M3, M4.

### M1: S-mode privilege and trap plumbing

Deliverable: the emulator boots in S-mode, the S-mode CSRs are readable and writable, `SRET` and `SFENCE.VMA` work, a delegated trap enters `stvec`, and `WFI` honors `mstatus.TW`.

- Add `RVE_E_SV32` to `RiscvEmulator/include/RiscvEmulatorConfig.h`. Sv32 implies Zicsr.
- Add the S-mode CSR storage to `RiscvEmulator/include/RiscvEmulatorTypeCSR.h`: `stvec`, `sscratch`, `sepc`, `scause`, `stval` and `scounteren` (a separate 32-bit value with a `0x7` write mask). Reuse `mstatus` for `sstatus`, and `mie` and `mip` for `sie` and `sip`.
- Add the write masks for `mstatus` S-mode bits, `medeleg`, `mideleg`, `satp`, `mie` S-mode bits and `mip.SSIP` to `RiscvEmulator/include/RiscvEmulatorTypeCSR.h`.
- Add the S-mode CSR cases to `RiscvEmulatorGetCSRAddress()` in `RiscvEmulator/include/RiscvEmulatorExtensionZicsr.h`.
- Add `SRET`, `SFENCE.VMA` and the `mstatus.TW` check to `RiscvEmulatorOpcodeSystem()` and `RiscvEmulatorWFI()` in `RiscvEmulator/include/RiscvEmulatorExtensionI.h`.
- Add `RiscvEmulatorTrapEnterSupervisor()` and the delegation choice to `RiscvEmulator/include/RiscvEmulatorTrap.h`.
- Add the S-mode CSR names and the `sret` and `sfence.vma` disassembly to `RiscvEmulatorDebug.h` and `RiscvEmulatorDisasm.h`.
- Set the S-mode boot state in `RiscvEmulator-Native/src/main.c`.

Validation: the ACT S-mode coverpoints.

### M2: Sv32 translation

Deliverable: every fetch, load and store goes through `RiscvEmulatorLoadVirtual()` or `RiscvEmulatorStoreVirtual()`; the walker handles valid and invalid PTEs, permissions, Svade, page faults and page-crossing accesses.

- Add `RiscvEmulator/include/RiscvEmulatorExtensionSv32.h` with the walker, the permission checks, the Svade check, the fault raising and the virtual load/store wrappers.
- Route the fetch in `RiscvEmulator.h` and the loads and stores in `RiscvEmulatorExtensionI.h`, `RiscvEmulatorExtensionC.h`, `RiscvEmulatorExtensionZcb.h` and `RiscvEmulatorExtensionA.h` through the wrappers.
- Add the three Sv32 environments to `generate-isa-extension-combination.py` and the `Sv32` subset to the ACT `gen_core.py`.

Validation: the ACT Svade and Sstvala coverpoints on the three Sv32 environments.

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
