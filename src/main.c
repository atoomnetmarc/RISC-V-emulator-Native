/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#define _POSIX_C_SOURCE 199309L

#include <fcntl.h>
#include <poll.h>
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

uint8_t pleasestop = 0;

size_t loopcounter = 0;

// Instruction limit set by -m <N>; 0 (the default) means unlimited.
size_t maxloopcounter = 0;

// Instruction count at which the verbose trace starts, set by -v <start>.
size_t verbose_start = 0;

// Progress interval set by -p <N>; 0 (the default) disables progress output.
size_t progress_interval = 0;

uint32_t testresult = 0;

uint8_t verbose = 0;

// Consumer override of the library's weak disassembly output function. The
// library renders each instruction via the shape functions; this override
// sends the fragments to stdout. The verbose gate lives here (a consumer
// concern): when tracing is off every call becomes a no-op.
#if (RVE_E_DISASM == 1)
void RiscvEmulatorDisasmPrintf(const char *fmt, ...) {
    if (verbose == 0 || loopcounter < verbose_start) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
#endif

// Drain all currently available stdin bytes into the emulated UART.
static void poll_stdin(void) {
    uint8_t ch;
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        uart16550_rx_push(&uart, ch);
    }
}

int main(int argc, char *argv[]) {
    // Parse -v [<start>] to enable verbose instruction tracing from an
    // instruction count, -m <N> to limit the number of simulated instructions
    // (0 = unlimited, the default), -p <N> to print the PC every N
    // instructions and -b <dtb> to load a device tree blob. The binary path is
    // the last argument; run_test.sh passes it via elf2bin.sh.
    const char *binpath = NULL;
    const char *dtbpath = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
            if (i + 1 < argc) {
                char *end;
                unsigned long start = strtoul(argv[i + 1], &end, 0);
                if (end != argv[i + 1] && *end == '\0') {
                    verbose_start = (size_t)start;
                    i++;
                }
            }
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            i++;
            char *end;
            maxloopcounter = strtoul(argv[i], &end, 0);
            if (*end != '\0') {
                printf("Error: -m requires a numeric argument, got '%s'.\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            i++;
            char *end;
            progress_interval = strtoul(argv[i], &end, 0);
            if (*end != '\0') {
                printf("Error: -p requires a numeric argument, got '%s'.\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            i++;
            dtbpath = argv[i];
        } else {
            binpath = argv[i];
        }
    }

    if (binpath == NULL) {
        printf("Usage: %s [-v [<start>]] [-m <instructions>] [-p <interval>] [-b <dtb>] <binary>\n", argv[0]);
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
    if (ferror(fbin)) {
        printf("Error reading %s.\n", binpath);
        fclose(fbin);
        free(memory);
        return 2;
    }
    if (verbose) {
        printf("Read %zu bytes.\n", binsize);
    }
    fclose(fbin);

    // Load the device tree blob into the reserved area at the top of RAM.
    // The board passes its physical address to the kernel in a1.
    uint32_t dtb_address = 0;
    if (dtbpath != NULL) {
        if (binsize > (size_t)RAM_LENGTH - (size_t)DTB_RESERVE) {
            free(memory);
            printf("Error: binary overlaps the DTB reserve.\n");
            return 2;
        }
        FILE *fdtb = fopen(dtbpath, "rb");
        if (fdtb == NULL) {
            free(memory);
            printf("file not found: %s\n", dtbpath);
            return 2;
        }
        fseek(fdtb, 0, SEEK_END);
        long dtbsize = ftell(fdtb);
        fseek(fdtb, 0, SEEK_SET);
        if (dtbsize <= 0 || (size_t)dtbsize > (size_t)DTB_RESERVE) {
            fclose(fdtb);
            free(memory);
            printf("Error: DTB must be between 1 and %u bytes.\n", (unsigned)DTB_RESERVE);
            return 2;
        }
        size_t dtb_offset = (size_t)RAM_LENGTH - (size_t)DTB_RESERVE;
        if (fread(&memory[dtb_offset], (size_t)dtbsize, 1, fdtb) != 1) {
            fclose(fdtb);
            free(memory);
            printf("Error reading %s.\n", dtbpath);
            return 2;
        }
        fclose(fdtb);
        dtb_address = RAM_ORIGIN + (uint32_t)dtb_offset;
        if (verbose) {
            printf("Loaded %ld bytes of DTB at 0x%08X.\n", dtbsize, dtb_address);
        }
    }

    if (verbose) {
        printf("RiscvEmulatorInit()\n");
    }

    RiscvEmulatorInit(&RiscvEmulatorState, RAM_LENGTH);

    // Linux expects the hart ID in a0 and the DTB physical address in a1.
    RiscvEmulatorState.reg.a0 = 0;
    RiscvEmulatorState.reg.a1 = dtb_address;

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
        RiscvDevicesTick(&RiscvEmulatorState);
        RiscvEmulatorLoop(&RiscvEmulatorState);

        if (progress_interval > 0 && loopcounter % progress_interval == 0) {
            printf("pc: 0x%08X, instructions: %zu\n",
                   RiscvEmulatorState.programcounter, loopcounter);
        }

        if (RiscvEmulatorIsWaiting(&RiscvEmulatorState)) {
            // The hart is parked in WFI. Poll stdin every idle iteration
            // (the 4096-instruction interval does not apply) and sleep
            // briefly so the host does not spin. The CLINT timer interrupt
            // is the architectural wake source.
            poll_stdin();
            poll(NULL, 0, 1);
        } else if (loopcounter % STDIN_POLL_INTERVAL == 0) {
            poll_stdin();
        }

        // If this prints then consider adding a hook in RiscvEmulatorHook.h and implementing it in hook.c.
        if (RiscvEmulatorState.instructionhandled == 0 &&
            RiscvEmulatorIsWaiting(&RiscvEmulatorState) == 0) {
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

    if (testresult == TEST_PASS) {
        return 0;
    }
    return 1;
}