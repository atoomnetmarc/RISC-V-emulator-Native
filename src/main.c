/*

Copyright 2023-2025 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <RiscvEmulator.h>

#include "memory.h"

uint8_t memory[RAM_LENGTH];

RiscvEmulatorState_t RiscvEmulatorState;

size_t loopcounter = 0;

uint32_t testresult = 0;

uint8_t verbose = 0;

int main(int argc, char *argv[]) {
    // Parse -v to enable verbose instruction tracing. The binary path is the
    // last argument; run_tests.py passes it via elf2bin.sh.
    const char *binpath = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else {
            binpath = argv[i];
        }
    }

    if (binpath == NULL) {
        printf("Usage: %s [-v] <binary>\n", argv[0]);
        return 2;
    }

    // Unbuffered stdout so the RVCP-SUMMARY line is not lost if the emulator halts.
    setvbuf(stdout, NULL, _IONBF, 0);
    pleasestop = 0;

    // Load the actual binary (objcopy -O binary output) directly into RAM.
    // Its byte 0 corresponds to address 0x80000000 (RAM_ORIGIN).
    FILE *fbin = fopen(binpath, "rb");
    if (fbin == NULL) {
        printf("file not found: %s\n", binpath);
        return 2;
    }
    size_t binsize = fread(memory, sizeof(uint8_t), sizeof(memory), fbin);
    if (verbose) {
        printf("Read %zu bytes.\n", binsize);
    }
    fclose(fbin);

    if (verbose) {
        printf("RiscvEmulatorInit()\n");
    }

    RiscvEmulatorInit(&RiscvEmulatorState, sizeof(memory));

    // RiscvEmulatorInit sets the PC to ROM_ORIGIN (0x20000000). ACT4 places
    // all code and data in RAM at RAM_ORIGIN (0x80000000), so override the PC
    // after init. Without this the emulator boots at the HALT region and crashes.
    RiscvEmulatorState.programcounter = RAM_ORIGIN;
    RiscvEmulatorState.programcounternext = RAM_ORIGIN;

    // Fixed safety net against a test that never halts.
    size_t maxloopcounter = 100000000;

    for (;;) {
        loopcounter++;
        RiscvEmulatorLoop(&RiscvEmulatorState);

        // If this prints then consider adding a hook in RiscvEmulatorHook.h and implementing it in hook.c.
        if (RiscvEmulatorState.hookexists == 0) {
            printf("pc: 0x%08X, instruction: 0x%08X, ???\n",
                   RiscvEmulatorState.programcounter,
                   RiscvEmulatorState.instruction.value);

            // pleasestop = 1;
        }

        if (RiscvEmulatorState.reg.x[0] != 0) {
            printf("Error: x0 must always be zero. x0 is now 0x%08X. Stop emulation.\n",
                   RiscvEmulatorState.reg.x[0]);
            pleasestop = 1;
        }

        if (loopcounter >= maxloopcounter) {
            printf("Loopcounter limit reached, stopping emulation.\n");
            break;
        }

        if (pleasestop > 0) {
            break;
        }
    }

    if (verbose) {
        printf("Simulated %zu CPU instructions.\n", loopcounter);
    }

    if (testresult == 123456789) {
        return 0;
    }
    printf("Test failed. testresult=0x%08X\n", testresult);
    return 1;
}