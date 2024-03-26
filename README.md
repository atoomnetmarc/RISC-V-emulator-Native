# Description

This is an implementation of my [RISC-V cpu emulator](https://github.com/atoomnetmarc/RISC-V-emulator) that can compile into an operating system executable using [PlatformIO Native development platform](https://docs.platformio.org/en/latest/platforms/native.html).

It is used for generating signature files that the [RISCOF](https://riscof.readthedocs.io/) test suite can test against the tests defined in [riscv-arch-test](https://github.com/riscv-non-isa/riscv-arch-test).

The resulting report.html that RISCOF generates gives an overview of RISV-V instructions that behave correctly or ones that need some work.

# Notes to self

Execute the program in the same directory as where `dut-rom.bin` (the RISC-V ROM image) and `dut-ram.bin` (the RISC-V RAM default values) exist. After executing you should get `dut-ram-after.bin`.

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
