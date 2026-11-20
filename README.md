# Description

This is an implementation of my [RISC-V cpu emulator](https://github.com/atoomnetmarc/RISC-V-emulator) that can compile into an operating system executable.

The platform devices (CLINT, 16550 UART, SYSCON, PLIC, virtio-blk, virtio-console, virtio-net, virtio-rng and goldfish RTC) come from the shared [RISC-V-emulator-Devices](https://github.com/atoomnetmarc/RISC-V-emulator-Devices) library. The port wires them in `RiscvEmulatorLoad` and `RiscvEmulatorStore`.

Two build systems are supported:

- [PlatformIO Native](README-platformio.md)
- [CMake](cmake/README-cmake.md) — compiler-matrix testing without PlatformIO

Both produce binaries for the RISC-V-emulator-ACT test wrapper in
`binaries/<compiler-tag>/<isa>`.

## Linux boot

The Linux target is the Sv32 image in [`RISC-V-emulator-Linux`](../RISC-V-emulator-Linux): OpenSBI at `0x80000000`, U-Boot at `0x80400000` and the kernel on the disk. Build the DTB from `configs/linux-mmu.dts`:

```sh
dtc -I dts -O dtb -o linux-mmu.dtb configs/linux-mmu.dts
```

The emulator loads the DTB at the address given with `-b` and passes its physical address in `a1`. The hart ID is `0` in `a0`. The DTS declares 64 MB of RAM, a CLINT at `0x02000000`, a 16550 UART at `0x10000000` on PLIC source 4, a SYSCON at `0x00100000`, virtio-blk at `0x10001000`, virtio-console at `0x10002000`, virtio-net at `0x10003000`, virtio-rng at `0x10004000` on PLIC source 5 and a goldfish RTC at `0x10005000` on PLIC source 6. The CPU node carries `riscv,isa-base` and `riscv,isa-extensions` for kernel ISA detection, alongside the legacy `riscv,isa` string.

## Multi-stage boot

`-l <address>,<file>` loads an image at a physical address; repeat it for each stage. The positional binary is optional and still loads at `0x80000000`. `-e <address>` sets the start PC; the default is `0x80000000`. For example, OpenSBI `fw_jump` at `0x80000000`, U-Boot at `0x80400000` and the DTB at `0x82200000` (the `FW_JUMP_FDT_OFFSET` target):

```sh
./binaries/gcc/RV32I-linux-mmu \
    -b 0x82200000,linux-mmu.dtb \
    -l 0x80000000,/path/to/opensbi/ilp32/generic/firmware/fw_jump.bin \
    -l 0x80400000,u-boot.bin
```

`-b <address>,<file>` loads the DTB at the given address and sets `a1`; `-b <file>` loads it at the top of RAM and sets `a1`. `-l` does not touch `a1`.

## Devices

- `-D <file>` attaches a disk image to virtio-blk.
- `-N <ifname>` attaches an existing TAP interface to virtio-net. The interface must already exist and be up. The emulator opens `/dev/net/tun` with `O_NONBLOCK`, verifies the flag with `fcntl`, and polls the descriptor with a zero timeout before reading, so an idle TAP never blocks the main loop.
- virtio-rng fills guest entropy buffers from the host `getrandom()` call. No command-line option is needed.
- The goldfish RTC samples the host `CLOCK_REALTIME` at init and advances from `CLOCK_MONOTONIC`. Guest writes change the emulated clock only. No command-line option is needed.

## Idle behaviour

When the guest executes `WFI`, the native board blocks in `ppoll()` until the CLINT timer deadline, stdin, or the TAP interface becomes ready. A programmed timer sleeps at most one second; without a timer the wait stays at one millisecond so the ACT timeout tests remain bounded. The emulator core keeps its finite-WFI behaviour.

At stop the emulator prints `real`, `user` and `sys` in the format of the Linux `time` command, plus `idle` as the time spent blocked in `ppoll()` and its share of `real`. A `WFI` line reports the number of waits, the wake reason counts and the longest sleep. The `Allocated` line reports the RAM size and the bytes loaded from files. The `RAM pages touched` line reports the count of 4 KiB pages touched by CPU stores and file loads, with the equivalent bytes, KiB and MiB. Virtio DMA writes are not counted.

## Debugging

- `-v [<start>]` prints the instruction trace, optionally from instruction `<start>`.
- `-p <interval>` prints the PC every `<interval>` instructions.
- `-m <instructions>` stops after `<instructions>` instructions.
- `-t [<count>]` logs traps with the machine CSRs and key registers, optionally only the first `<count>` traps.
- `-r` dumps all integer registers and the machine CSRs at stop.
- `-d <address>,<length>` hexdumps `<length>` bytes of RAM from `<address>` at stop.
- `-s <address>` stops before the instruction at `<address>` and dumps the registers.
- `-w <address>[,<length>]` stops on the first load or store that overlaps the range and dumps the registers.
- `-S <prefix>` enables snapshots and writes `<prefix>.json` with the CPU, CSR and device state and `<prefix>.bin` with the 64 MiB guest RAM.
- `-R <prefix>` restores a snapshot written by `-S` and continues from the saved instruction and PC.
- `-C` forwards Ctrl-C to the guest as byte `0x03` instead of terminating the host on SIGINT. Use it with MicroPython to raise `KeyboardInterrupt`; stop a guest that ignores the byte with `kill`.

With `-S <prefix>`, send `SIGUSR1` to the emulator process to request a snapshot. The signal handler only sets a flag; the main loop writes both files at the next instruction boundary and the emulation continues. Files are written to temporary names and renamed when complete, so a partial snapshot is never published. The JSON contains a magic value, a format version, the ISA flags, the instruction count, the PC, all integer registers, the CSRs, the trap and WFI state, and the registers of the CLINT, PLIC, UART, virtio devices and Goldfish RTC. 64-bit values are decimal strings so no digits are lost in parsers that use IEEE-754 numbers. Without `-S`, `SIGUSR1` keeps its default action.

`-R <prefix>` reads both files, checks the header and ISA flags, restores the CPU and device state, and re-anchors the CLINT and RTC to the host clock at the moment of restore. The disk image and TAP interface are external state: use the same disk image as when the snapshot was written, and a restored TAP connection is not possible. The same snapshot can be loaded by `simavr-host -R <prefix>` to continue the boot on SimAVR.

## Verification

After any source change (also in the emulator repo), run both:

- `pio run` — PlatformIO build
- `python3 cmake/run-matrix.py --all` — full compiler matrix

The matrix catches compiler-specific warnings that a single-compiler build
will not (e.g. clang's `-Wsign-conversion`).

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
