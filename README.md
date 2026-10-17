# Description

This is an implementation of my [RISC-V cpu emulator](https://github.com/atoomnetmarc/RISC-V-emulator) that can compile into an operating system executable.

Two build systems are supported:

- [PlatformIO Native](README-platformio.md)
- [CMake](cmake/README-cmake.md) — compiler-matrix testing without PlatformIO

Both produce binaries for the RISC-V-emulator-ACT test wrapper in
`binaries/<compiler-tag>/<isa>`.

## Verification

After any source change (also in the emulator repo), run both:

- `pio run` — PlatformIO build
- `python3 cmake/run-matrix.py --all` — full compiler matrix (gcc + clang)

The matrix catches compiler-specific warnings that a single-compiler build
will not (e.g. clang's `-Wsign-conversion`).

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
