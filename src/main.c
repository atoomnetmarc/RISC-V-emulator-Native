/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// The consumer provides its own RiscvEmulatorDisasmPrintf below; skip the
// library's weak definition (it cannot be overridden in the same TU).
#define RVE_DISASM_PRINTF_OVERRIDE 1

#include <RiscvEmulator.h>

#include "memory.h"

uint8_t *memory;

RiscvEmulatorState_t RiscvEmulatorState;

size_t loopcounter = 0;

// Instruction limit set by -m <N>; 0 (the default) means unlimited.
size_t maxloopcounter = 0;

uint32_t testresult = 0;

uint8_t verbose = 0;

// Consumer override of the library's weak disassembly output function. The
// library renders each instruction via the shape functions; this override
// sends the fragments to stdout. The verbose gate lives here (a consumer
// concern): when tracing is off every call becomes a no-op.
#if (RVE_E_DISASM == 1)
void RiscvEmulatorDisasmPrintf(const char *fmt, ...) {
    if (verbose == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
#endif

int main(int argc, char *argv[]) {
    // Parse -v to enable verbose instruction tracing and -m <N> to limit the
    // number of simulated instructions (0 = unlimited, the default). The
    // binary path is the last argument; run_test.sh passes it via elf2bin.sh.
    const char *binpath = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            i++;
            maxloopcounter = strtoul(argv[i], NULL, 0);
        } else {
            binpath = argv[i];
        }
    }

    if (binpath == NULL) {
        printf("Usage: %s [-v] [-m <instructions>] <binary>\n", argv[0]);
        return 2;
    }

    // Unbuffered stdout so the RVCP-SUMMARY line is not lost if the emulator halts.
    setvbuf(stdout, NULL, _IONBF, 0);
    pleasestop = 0;

    memory = malloc(RAM_LENGTH);
    if (memory == NULL) {
        printf("Failed to allocate %zu bytes of RAM.\n", (size_t)RAM_LENGTH);
        return 2;
    }
    memset(memory, 0, RAM_LENGTH);

    // Load the actual binary (objcopy -O binary output) directly into RAM.
    // Its byte 0 corresponds to address 0x80000000 (RAM_ORIGIN).
    FILE *fbin = fopen(binpath, "rb");
    if (fbin == NULL) {
        free(memory);
        printf("file not found: %s\n", binpath);
        return 2;
    }
    size_t binsize = fread(memory, sizeof(uint8_t), RAM_LENGTH, fbin);
    if (verbose) {
        printf("Read %zu bytes.\n", binsize);
    }
    fclose(fbin);

    if (verbose) {
        printf("RiscvEmulatorInit()\n");
    }

    RiscvEmulatorInit(&RiscvEmulatorState, RAM_LENGTH);

    // RiscvEmulatorInit sets the PC to ROM_ORIGIN (0x20000000). ACT4 places
    // all code and data in RAM at RAM_ORIGIN (0x80000000), so override the PC
    // after init. Without this the emulator boots at the HALT region and crashes.
    RiscvEmulatorState.programcounter = RAM_ORIGIN;
    RiscvEmulatorState.programcounternext = RAM_ORIGIN;

    // maxloopcounter is 0 (the default) unless -m <N> is given, meaning the
    // emulation runs until the test signals a halt.

    // Put stdin in raw, non-blocking mode so typed characters reach the
    // emulated UART immediately.
    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios term = orig_termios;
    term.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
    int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);

    // Polling stdin on every instruction slows the simulation drastically,
    // so only do it once every STDIN_POLL_INTERVAL instructions.
    #define STDIN_POLL_INTERVAL 4096

    for (;;) {
        loopcounter++;
        RiscvEmulatorLoop(&RiscvEmulatorState);

        if (loopcounter % STDIN_POLL_INTERVAL == 0) {
            uint8_t ch;
            while (read(STDIN_FILENO, &ch, 1) == 1) {
                uart_rx_push(ch);
            }
        }

        // If this prints then consider adding a hook in RiscvEmulatorHook.h and implementing it in hook.c.
        if (RiscvEmulatorState.instructionhandled == 0) {
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

        if (maxloopcounter > 0 && loopcounter >= maxloopcounter) {
            printf("Loopcounter limit reached, stopping emulation.\n");
            break;
        }

        if (pleasestop > 0) {
            break;
        }
    }

    printf("Simulated %zu CPU instructions.\n", loopcounter);
    printf("Allocated %zu bytes of RAM (%zu bytes used by the binary).\n",
           (size_t)RAM_LENGTH, binsize);
    free(memory);

    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

    if (testresult == 123456789) {
        return 0;
    }
    return 1;
}