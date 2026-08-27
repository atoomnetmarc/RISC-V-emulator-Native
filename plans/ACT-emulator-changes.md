# ACT Emulator Changes

This document specifies the code changes needed in this emulator to run ACT4 self-checking ELFs. The integration plan and milestones live in `RISC-V-emulator-ACT/plans/`.

The emulator loads a raw binary produced by `objcopy` from an ACT4 ELF. The binary starts at address `0x80000000` (`RAM_ORIGIN`). The emulator boots at `0x80000000`.

---

## Memory map

| Address      | Region           | Use                           |
| ------------ | ---------------- | ----------------------------- |
| `0x80000000` | RAM (`memory[]`) | Test code and data. Boot PC.  |
| `0x10000000` | UART             | Console output. Write hook.   |
| `0x20000000` | HALT             | Test termination. Write hook. |

These match the `rvmodel_macros.h` in `RISC-V-emulator-ACT/config/cores/atoomnetmarc/*/`.

---

## Change 1: UART store hook at `0x10000000`

File: `include/RiscvEmulatorImplementationSpecific.h`, function `RiscvEmulatorStore`.

The ACT4 `RVMODEL_IO_WRITE_STR` macro writes each character as a word (`sw`) to `0x10000000`. The character is in the low byte; the upper bytes are zero. The emulator must emit the low byte to stdout so the `RVCP-SUMMARY: TEST PASSED/FAILED` line appears in the test log.

Add a branch at the top of `RiscvEmulatorStore`, before the existing `RAM_ORIGIN` / `ROM_ORIGIN` / `IO_ORIGIN` checks:

```c
if (address == 0x10000000) {
    uint8_t ch;
    memcpy(&ch, source, 1);
    putchar(ch);
    return;
}
```

`putchar` writes to stdout, which `run_tests.py` captures into the per-test log. Set stdout to unbuffered at the start of `main` (`setvbuf(stdout, NULL, _IONBF, 0)`) so the summary line is not lost if the emulator halts.

---

## Change 2: HALT store hook at `0x20000000`

File: `include/RiscvEmulatorImplementationSpecific.h`, function `RiscvEmulatorStore`.

The ACT4 `RVMODEL_HALT_PASS` macro writes `123456789` to `0x20000000` then `0` to `0x20000004`. `RVMODEL_HALT_FAIL` writes `1` to `0x20000000`. Both macros end in an infinite self-loop, so the emulator must stop on the store.

Add a global in `include/memory.h`:

```c
extern uint32_t testresult;
```

Define it in `src/main.c`:

```c
uint32_t testresult = 0;
```

Add a branch at the top of `RiscvEmulatorStore`:

```c
if (address == 0x20000000) {
    memcpy(&testresult, source, 4);
    pleasestop = 1;
    return;
}
```

Ignore the second store to `0x20000004`. The value `123456789` means pass; any other value means fail.

---

## Change 3: Load a single binary from `argv[1]`

File: `src/main.c`.

Replace the `dut-ram.bin` / `dut-rom.bin` loading with a single binary load. The binary is the output of `riscv64-unknown-elf-objcopy -O binary <elf> <bin>`. Its byte 0 corresponds to address `0x80000000`, so it loads directly into `memory[]`.

```c
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <binary>\n", argv[0]);
        return 2;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    pleasestop = 0;

    FILE *fbin = fopen(argv[1], "rb");
    if (fbin == NULL) {
        printf("file not found: %s\n", argv[1]);
        return 2;
    }
    size_t binsize = fread(memory, sizeof(uint8_t), sizeof(memory), fbin);
    fclose(fbin);

    RiscvEmulatorInit(&RiscvEmulatorState, sizeof(memory));
    // PC starts at 0x80000000 (RAM_ORIGIN), the reset vector.
```

---

## Change 4: Delete dead code

File: `src/main.c`.

Delete:

- The `firmware[]` array and its uses (ROM is unused; ACT4 places everything in RAM).
- The `dut-rom.bin` read.
- The `dut-ram-signature_begin_end.txt` read and the `signaturebegin` / `signatureend` logic.
- The `dut-ram-after.bin` write.
- The `DUT-rve.signature` write and its loop.
- The `maxloopcounter` logic based on ROM size. Replace with a fixed large limit or remove it (the HALT hook stops the emulator).

File: `include/memory.h`.

Delete the `firmware[]` array if no other code references it.

---

## Change 5: Return the test result

File: `src/main.c`.

After the emulation loop, return the test result:

```c
    printf("Simulated %zu CPU instructions.\n", loopcounter);

    if (testresult == 123456789) {
        return 0;
    }
    printf("Test failed. testresult=0x%08X\n", testresult);
    return 1;
}
```

`run_tests.py` treats exit code 0 as pass and nonzero as fail. It also requires the `RVCP-SUMMARY` line on stdout. Both must agree.

---

## What stays the same

- `RiscvEmulatorInit` and `RiscvEmulatorLoop` are unchanged.
- The `RiscvEmulatorLoad` function is unchanged (loads from `memory[]` for `RAM_ORIGIN` addresses).
- The `RiscvEmulatorIllegalInstruction`, `RiscvEmulatorUnknownCSR`, `RiscvEmulatorHandleECALL`, and `RiscvEmulatorHandleEBREAK` hooks are unchanged. The `ecall a7=93` path in `RiscvEmulatorHandleECALL` becomes unreachable for ACT4 tests (they use the HALT store hook), but leave it in place; it does no harm.
- The build flags (`RVE_E_*`) are set per config by the PlatformIO environment, not by this file.
