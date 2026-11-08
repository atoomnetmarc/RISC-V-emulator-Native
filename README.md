# Description

This is an implementation of my [RISC-V cpu emulator](https://github.com/atoomnetmarc/RISC-V-emulator) that can compile into an operating system executable.

The platform devices (CLINT, 16550 UART, SYSCON, PLIC) come from the shared [RISC-V-emulator-Devices](https://github.com/atoomnetmarc/RISC-V-emulator-Devices) library. The port wires them in `RiscvEmulatorLoad` and `RiscvEmulatorStore`.

Two build systems are supported:

- [PlatformIO Native](README-platformio.md)
- [CMake](cmake/README-cmake.md) — compiler-matrix testing without PlatformIO

Both produce binaries for the RISC-V-emulator-ACT test wrapper in
`binaries/<compiler-tag>/<isa>`.

## Linux boot

Build a DTB from the sample DTS and pass it with `-b`:

```sh
dtc -I dts -O dtb -o linux.dtb configs/linux.dts
./binaries/gcc/RV32I -b linux.dtb Image
```

The emulator loads the DTB into the top 16 KB of RAM and passes its physical address in `a1`. The hart ID is `0` in `a0`. The sample DTS declares 64 MB of RAM, a CLINT at `0x02000000`, a 16550 UART at `0x10000000` and a SYSCON at `0x00100000`.

## Debugging

- `-v [<start>]` prints the instruction trace, optionally from instruction `<start>`.
- `-p <interval>` prints the PC every `<interval>` instructions.
- `-m <instructions>` stops after `<instructions>` instructions.

## Verification

After any source change (also in the emulator repo), run both:

- `pio run` — PlatformIO build
- `python3 cmake/run-matrix.py --all` — full compiler matrix

The matrix catches compiler-specific warnings that a single-compiler build
will not (e.g. clang's `-Wsign-conversion`).

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
