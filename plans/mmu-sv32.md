# Sv32 MMU Plan

## Goal

Run the Linux kernel with paged virtual memory on the RV32 emulator, using the smallest Sv32 implementation that Linux supports.

## Scope

In scope:

- S-mode and U-mode privilege.
- Sv32 two-level page tables with 4 KiB pages.
- OpenSBI -> U-Boot -> Linux boot chain.
- S-mode timer and external interrupts.
- Two MMU Buildroot images.

Out of scope:

- TLB.
- 4 MiB superpages.
- ASID.
- MPRV.
- PMP.
- Svadu, Svpbmt and Svnapot.
- SMP.
- Hypervisor extension.

## Status

M1 (S-mode), M2 (Sv32), M3 (S-mode devices) and M4 (Linux boot) are done; the MMU images boot to a shell. M5 (no-MMU cleanup) remains.

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

`WFI` parks the hart for one idle iteration and then resumes, in every privilege mode. The implementation completes the wait within the bounded time limit, so `mstatus.TW` does not raise an illegal-instruction exception.

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

`mip` and `mie` gain bits 1 (SSIP), 3 (MSIP), 5 (STIP), 7 (MTIP), 9 (SEIP) and 11 (MEIP). `mip.SSIP` is writable by S-mode software. `mip.STIP` and `mip.SEIP` are read-only and are set by the CLINT and PLIC. `mip.MSIP` is set by the CLINT `MSIP` register and `mip.MTIP` by the CLINT `mtimecmp` register. `mip.MEIP` is set by the platform interrupt controller.

The platform interrupt controller is a custom CSR pair: `0x8C0` sets and `0x8C1` clears the software-set platform bits in `mip_platform_software`. The device tick writes the device-driven bits in `mip_platform`, so the two sources do not overwrite each other. A `mip` read and the interrupt selection use the OR of `mip`, `mip_platform` and `mip_platform_software`. The ACT macros use `csrw` to these CSRs, so the M-, S- and U-mode tests raise MEI and SEI without T-SBI or the identity map. The board also has a memory-mapped simple interrupt generator at 0x0C400000 for the same purpose.

`mideleg` accepts bits 1, 5 and 9. `medeleg` accepts the S-mode exception bits: 0 to 8, 12, 13 and 15. Bit 9 (ECALL from S-mode) and bit 11 (ECALL from M-mode) are not delegated.

The interrupt selection checks the non-delegated interrupts before the delegated ones, because interrupts to M-mode take priority over interrupts to S-mode. Within each group the priority is MEI, MSI, MTI, SEI, SSI, STI. A delegated interrupt enters S-mode through `stvec`, `scause`, `sepc` and `sstatus` only when the active privilege is S-mode with `sstatus.SIE` set or U-mode; it never enters M-mode.

### Boot

The RISC-V manual requires M-mode at reset (`machine.adoc`, `m_mode_priv_at_reset`). The emulator starts the hart in M-mode at `0x80000000`. The ACT tests run in M-mode and the framework's `RVTEST_BOOT_TO_MMODE` sets up the M-mode CSRs and trap handlers.

Linux with `CONFIG_MMU=y` must run in S-mode because `satp` is an S-mode CSR and is active only in S-mode or U-mode (`supervisor.adoc`). M-mode has no address translation. The MMU image needs an SBI and an M-mode firmware that switches to S-mode. OpenSBI provides both: the emulator loads `fw_jump` at `0x80000000` and OpenSBI jumps to U-Boot at `0x80400000`.

The DTB describes the CPU with `mmu-type = "riscv,sv32"`. The CLINT uses S-mode interrupt causes 1 and 5. The PLIC uses S-mode interrupt cause 9. The memory node keeps 64 MiB at `0x80000000`; the `-M` option is deferred. The U-Boot default environment sets `kernel_addr_r=0x80600000`, `fdt_addr_r=0x81b00000` and `ramdisk_addr_r=0x81c00000`; the boot command `ext4load`s the `Image` and the DTB from the boot partition and runs `booti`. The `bootargs` live in the DTB `/chosen/bootargs`.

### Devices

The CLINT sets `mip.STIP` when the timer compare value is reached. The PLIC delivers external interrupts through `mip.SEIP`. The PLIC context and the DTS `interrupts-extended` cells use the S-mode causes.

The virtio-mmio transport resets on a zero `Status` write, so a second driver starts from a clean transport. The PLIC enable registers use the `0x80`-byte context stride, so the S-mode context receives the virtio interrupt.

### Buildroot

The MMU image uses:

- `CONFIG_MMU=y`
- `CONFIG_RISCV_SBI=y`
- `CONFIG_RISCV_M_MODE=n`
- `CONFIG_SMP=n`

The image keeps the existing kernel version, the ext4 disk root and the virtio devices. The libc is uClibc-ng. U-Boot loads the kernel `Image` at `0x80600000`; OpenSBI `fw_jump` is the firmware.

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
| 2   | OpenSBI -> U-Boot -> Linux: the emulator loads OpenSBI `fw_jump` at `0x80000000` and U-Boot at `0x80400000`; U-Boot loads Linux from the disk                        | The realistic boot chain; OpenSBI provides the SBI and U-Boot loads the kernel from the disk |
| 3   | Core-level `RiscvEmulatorTranslate(state, va, access)` ahead of every fetch, load and store; the board keeps physical `RiscvEmulatorLoad` and `RiscvEmulatorStore` | One translation path, a testable MMU and a board that stays physical                                   |
| 4   | S-mode PLIC and CLINT with `mideleg` bits 1, 5 and 9, and `mip.SEIP` and `mip.STIP`                                                                                | One trap path and one `stvec` handler, which matches the S-mode MMU                                    |
| 5   | Two MMU images: minimal and maximal                                                                                                                                | The minimal image is the lean target; the maximal image exercises every implemented extension          |
| 6   | Svade: a page fault when A is clear, or when a store finds D clear                                                                                                 | The ACT Svade coverpoint is the only A/D behaviour the test framework can check; hardware update has no ACT coverpoint |
| 7   | uClibc-ng for the MMU image                                                                                                                                        | Reuses the existing defconfig, patches and rootfs overlay; the smallest diff                           |
| 8   | `scounteren` is a separate CSR with a `0x7` write mask                                                                                                             | The manual requires a separate CSR at `0x106`; only CY, TM and IR are implemented                      |
| 9   | ACT coverpoints and the Linux boot                                                                                                                                 | The ACT coverpoints cover the ISA; the Linux boot is the integration test                              |
| 10  | `static inline` virtual load/store wrappers plus a fetch wrapper; call sites change one function name                                                              | One source copy of page-crossing and fault logic; the compiler chooses inlining; no `noinline`         |
| 11  | A page-crossing access is split into two chunks at the page boundary                                                                                               | Correct, at most two translations, lowest AVR cost                                                     |
| 12  | `stval` holds the faulting virtual address, also for a PTE access fault                                                                                            | The manual requires it (Sstvala)                                                                       |
| 13  | One level loop that treats any level as a leaf; the physical address mask depends on the level                                                                     | The manual defines megapages; one path removes the level-0 special case                                |
| 14  | `gen_core.py` sets `fs_legal_states` and `vs_legal_states` to `ExtContext_Off` when F and V are absent                                                              | The reference config must not declare features the DUT does not have                                  |
| 15  | ACT `act4` commit `1c3e9f2a` with the per-chunk trap signature counts and one temporary trap-handler-width patch                                                    | A global `TRAP_SIGUPD_COUNT` cannot serve both the small interrupt tests and the large medeleg walk; upstream #2505 sizes the region per test chunk                    |
| 16  | Exclude the U-mode tests that trap-loop under the boot identity map and `Sv_sv32_satp_access_Smode` through `# act-exclude-extensions` in the Sv32 envs | The boot identity map is not user accessible; tests that run U-mode code without `begin_sv_test` trap-loop in the reference model. The container applies only the trap-handler patch |
| 17  | Move `.text.rvmodel` directly after `.text.rvtest` in `link.ld`, before `.rodata` and `.data`                                                                       | The signature in `.data` pushes `.text.rvmodel` beyond the `R_RISCV_JAL` range for `InterruptsS-00`; the reorder keeps the call in range without an ACT source patch   |
| 18  | `senvcfg` is a read/write CSR with only FIOM writable; `sepc`/`mepc` clear the low bits per IALIGN                                                                  | The manual makes FIOM writable when `satp.MODE` is not read-only zero and the unimplemented extension fields read-only zero; with IALIGN=32 the two low bits of `sepc`/`mepc` are always zero |
| 19  | The `-t` trap trace is the primary diagnostic for the `Sv_Exceptions` group; the failure-message path stays unchanged until the tests pass                            | The trace logs every trap with `scause`, `sepc` and `stval`, so the first unexpected trap is visible without the console; the console fix is a separate tooling improvement |
| 20  | The AMO path translates once as a store/AMO access, then does the physical load and store                                                                          | The ISA treats an AMO as one store/AMO access, so a page fault is cause 15; the load-first path raises cause 13 when the load-side check fails |
| 21  | A misaligned AMO check is added only when the trace shows cause 6 or an access fault                                                                               | The config declares `MISALIGNED_AMO: false`; the manual makes the priority relative to page faults implementation-defined, and the config uses low priority |
| 22  | `S_scsr_ro-00` is investigated against the test source and the reference model before a fix                                                                        | The manual makes `cycle` a read-only shadow of `mcycle` that increments, and `mcountinhibit` stops it; an unimplemented `mcountinhibit` behaves as zero |
| 23  | The Zicfiss tests are excluded and only the M-mode CSR tests are fixed                                                                                             | Zicfiss is outside the plan's scope; the M-mode CSR tests are the real gaps                                                              |
| 24  | A misaligned LR/SC raises an access fault before translation                                                                                                       | The reference config declares `LRSC_MISALIGNED_BEHAVIOR: always raise access fault`; the check must precede the page walk so the access fault wins over a page fault |
| 25  | The user counters `0xC00`-`0xC82` are read-only                                                                                                                    | The manual makes `cycle`, `time` and `instret` read-only shadows of the machine counters; a `csrrw` to them raises an illegal instruction |
| 26  | `medeleg` delegates bits 0-10, 12, 13, 15, 18 and 19                                                                                                               | The generated reference config declares `0x000c_b1FF`; software check (18) and hardware error (19) are delegatable |
| 27  | `mip.SSIP`, `mip.STIP` and `mip.SEIP` are writable; `sip.SSIP` is writable and `sip.STIP`/`sip.SEIP` are read-only                                                  | The manual makes the `mip` bits writable (no `stimecmp`) and the `sip` timer/external bits read-only |
| 28  | `menvcfg.FIOM` is writable; `marchid` reads 1; `mhpmcounter3` is writable                                                                                          | The generated reference config declares `archid = 1` and HPM counter 3 writable; `FIOM` is writable because `satp` is implemented |
| 29  | `mret` and `sret` clear `mstatus.MPRV` when they return to a mode less privileged than M                                                                           | The manual requires it; the emulator kept MPRV set |
| 30  | The RV32 upper-half CSRs `0x312`-`0x314`, `0x318`-`0x319` and `0x31C`-`0x31F` are read-only zero                                                                   | The manual defines `medelegh`, `midelegh`, `mieh`, `mvienh`, `mviph` and `mstateen0h`-`mstateen3h`; the upper halves are zero on RV32 |
| 31  | The CLINT `mtime` advances by the host-clock delta, so a software write survives the next tick                                                                     | The ACT `cp_mtime_write` test writes `mtime` and reads the `time` CSR; replacing `mtime` with the host clock would discard the write |
| 32  | A write to `mcycle` or `minstret` is the final counter value; the writing instruction does not increment the counter                                                | The ACT `cp_minstret_wraparound` test expects the written value to be the final value |
| 33  | A trapping instruction does not increment `minstret`                                                                                                                | The ACT `cp_instret_ecall` test expects the ecall not to count as a retired instruction |
| 34  | WFI is finite: the hart resumes after one idle iteration, and `mstatus.TW` does not raise an illegal instruction                                                    | The generated config declares `WFI_FINITE`; the manual makes the TW trap conditional on the WFI not completing within the bounded time limit, and the ACT `cp_instret_wfi_timeout` test expects the WFI to return |
| 35  | The `sstatus` write preserves every `mstatus` bit outside the sstatus write mask                                                                                    | The ACT `cp_sret_s` test writes `sstatus` and expects `mstatus.TSR` to stay set |
| 36  | The `misa` C bit is read-only set                                                                                                                                    | The ACT `cp_misa_clear_c` test expects the C bit to stay set after a clear |
| 37  | The interrupt selection checks the non-delegated interrupts before the delegated ones, then uses the priority MEI, MSI, MTI, SEI, SSI, STI within each group, and checks the target mode's global enable | The manual gives M-mode interrupts priority over S-mode interrupts; a delegated interrupt traps to S-mode only in S-mode with `sstatus.SIE` set or in a less-privileged mode, and never to M-mode |
| 38  | The PLIC has an M-mode and an S-mode context                                                                                                                         | The S-mode external interrupt needs a context that the S-mode software can enable and claim |
| 39  | `mip.SEIP` is split into a software-writable bit and a platform signal; a read returns the OR and a write only changes the software bit                              | The manual requires the split for the CSR read-modify-write sequence; `sip.SEIP` is read-only |
| 40  | The ACT `RVMODEL_SET_SEXT_INT`, `RVMODEL_CLR_SEXT_INT` and `RVMODEL_SET_SSW_INT` macros are injected through `# act-define` in the `.ini` generator                     | The ACT template stays generic and the emulator `.ini` owns the platform macro |
| 41  | MEI and SEI are raised through a custom platform CSR: `0x8C0` sets and `0x8C1` clears the software-set bits in `mip_platform_software`; MSI and MTI use the CLINT `MSIP` and `mtimecmp` registers | The manual makes `meip` platform-specific; a CSR works in every mode without T-SBI or the identity map, and the separate word keeps the device tick from overwriting the software-set bits |
| 42  | The emulator `-M <size>` option is deferred; the first boot uses the fixed 64 MiB RAM and the current DTS | The first boot needs 64 MiB; the option is an experiment tool, not a boot requirement |
| 43  | The default environment sets `kernel_addr_r=0x80600000`, `fdt_addr_r=0x81b00000` and `ramdisk_addr_r=0x81c00000` | The three addresses sit below the relocated U-Boot stack at `0x81e8a000`; the QEMU defaults `0x84000000` and `0x8c000000` are outside the 64 MiB RAM |
| 44  | U-Boot builds with the minimal ISA `rv32ima_zicsr_zifencei`; C, F and D are off | The emulator has I, M, A, C, Zicsr, Zifencei and Zicntr; U-Boot does not need C, F or D. A is required: `arch/riscv/cpu/start.S` uses `amoswap.w` for the hart lottery and the available-harts lock |
| 45  | `CONFIG_ENV_IS_NOWHERE=y`; the compiled-in environment is the configuration | The disk is a Buildroot product, so a saved environment disappears on the next build; the embedded environment is reproducible from the U-Boot build |
| 46  | The boot command is embedded in `CONFIG_BOOTCOMMAND` and the load addresses in a `.env` file through `CONFIG_ENV_SOURCE_FILE` | The disk is regenerated, so a script on it is no more persistent than the binary; embedding removes the `mkimage` and ext4-script failure modes. The `.env` file keeps the addresses as environment variables |
| 47  | The emulator `-l <address>,<file>` option loads an image at a physical address; repeatable. The positional binary is optional and loads at `0x80000000`. `-e <address>` sets the initial PC (default `0x80000000`). `-b [<address>,]<file>` loads the DTB at the given address (default the top of RAM) and sets `a1` | The OpenSBI -> U-Boot chain needs three images at fixed addresses; `-l`, `-e` and the `-b` address keep the ACT positional-binary contract and put the DTB where `fw_jump` passes it |
| 48  | A boot partition on the disk holds the `Image` and the DTB; U-Boot `ext4load`s them | The 21 MB `Image` does not fit the 16 MB rootfs, and the disk path is the realistic boot |
| 49  | One DTB for OpenSBI, U-Boot and Linux | The hardware is identical for all three stages; a second DTB adds a sync burden |
| 50  | `bootargs` live only in the DTB `/chosen/bootargs`; the U-Boot environment leaves `bootargs` unset | One source; U-Boot overwrites the DTB value only when the environment variable is set |
| 51  | The first boot reads the console; the `-t` trap trace is added only on a hang | The console is the fastest signal; the trace is large because demand paging raises expected page faults |

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
| Minimal M-mode SBI stub                   | Extra binary and load stage; the emulator-hosted handler is smaller                                                                                                         |
| M-mode PLIC and CLINT with SBI forwarding | Splits the trap path over two privilege levels                                                                                                                              |
| Polling                                   | The 8250 and virtio drivers need interrupts                                                                                                                                 |
| Separate branch or repo                   | Duplication and double maintenance                                                                                                                                          |
| Parameterized build matrix                | More Makefile logic; the two images follow the existing pattern                                                                                                             |

## M5: no-MMU cleanup

Deliverable: the MMU images are the only Linux target; the no-MMU images, configs, DTBs and documentation are gone.

1. Inventory: grep for `nommu`, `no-MMU`, `linux-min`, `linux-max`, `RV32I-linux-min`, `RV32I-linux-max`, `output-max`, `genimage.cfg`, `linux.dts` and `linux-max.dts`.
2. PlatformIO: remove the four no-MMU environments; fold the `[linux-min]` and `[linux-max]` flags into `[linux-mmu]` and `[linux-mmu-max]`; keep the MMU `-disasm` environments; update `.vscode/launch.json`.
3. Makefile: remove the `MIN`/`MAX` variables and targets; make the MMU targets the default (`build`, `disk`, `run`, `defconfig`, `clean`); keep the max variants; set `EMULATOR` to the MMU max binary.
4. Configs: remove `rv32i_nommu_defconfig`, `kernel_config` and `genimage.cfg`; remove `configs/linux.dts` and `configs/linux-max.dts`; point the MMU DTS comments at `genimage-mmu.cfg`; rewrite `rv32i-linux/docs/disk-root.md` for the boot and rootfs partitions.
5. Documentation: rewrite `rv32i-linux/README.md` for the MMU target; update the Native `README.md` Linux boot example; update the scope, decision 5 and alternatives in this plan; update `RiscvEmulator-Devices/docs/diskio.md` for the two-partition disk and `root=PARTUUID`; check `RiscvEmulator-u-boot/docs/design.md`.
6. Generated artefacts: remove `buildroot/output/`, `output-max/`, `linux-min.dtb`, `linux-max.dtb`, the no-MMU emulator binaries and the PlatformIO build cache of the removed environments.
7. Validation: `pio run` in the Native, AVR and SAM projects; `python3 cmake/run-matrix.py --all`; `make build` and `make run` in `rv32i-linux`; `make build-mmu-max` and `make run-mmu-max`; `make build` in `RiscvEmulator-u-boot`; a final grep for the removed names.

The `[linux-min]` and `[linux-max]` flags are the base of the MMU environments. The flags move first, and `pio run -e RV32I-linux-mmu` confirms the MMU build before the no-MMU environments disappear.

## Validation

- `riscv-arch-test` S-mode coverpoints: `coverpoints/priv/S_coverage.svh`, `coverpoints/priv/Svade_coverage.svh` and `coverpoints/priv/Sstvala_coverage.svh`, run on `RV32IZicntr_Zicsr_Sv32`, `RV32IMAZicntr_Zicsr_Zifencei_Sv32` and `RV32IMACBZicntr_Zicsr_Zifencei_Sv32`.
- OpenSBI banner, U-Boot prompt and Linux boot with `make run-mmu`, a shell prompt and a working console.
- A trap trace with `-t` to confirm that page faults and S-mode interrupts arrive at `stvec`.

The ACT coverpoints cover the ISA; the Linux boot is the integration test.

## Open items

1. Run the Svade and Sstvala coverages on the three Sv32 environments.
2. Add the emulator `-M <size>` option and the DTB `/memory` fixup for the 8 and 16 MiB experiments.

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
