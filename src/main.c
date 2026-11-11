/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// The consumer provides its own RiscvEmulatorDisasmPrintf and
// RiscvEmulatorHook below; skip the library's weak definitions (they cannot
// be overridden in the same TU).
#define RVE_DISASM_PRINTF_OVERRIDE 1
#define RVE_HOOK_OVERRIDE          1

#include <RiscvEmulator.h>

#include "memory.h"

uint8_t *memory;

// Disk file descriptor, opened by -D <file>.
int disk_fd = -1;

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

// Trap logging set by -t [<count>]; 0 means unlimited.
uint8_t trap_log = 0;
size_t trap_log_limit = 0;
size_t trap_count = 0;

// Register dump set by -r.
uint8_t register_dump = 0;

// Memory dump set by -d <address>,<length>.
uint8_t memory_dump = 0;
uint32_t memory_dump_address = 0;
uint32_t memory_dump_length = 0;

// Breakpoint set by -s <address>.
uint8_t breakpoint = 0;
uint32_t breakpoint_address = 0;

// Watchpoint set by -w <address>[,<length>].
uint8_t watchpoint = 0;
uint32_t watchpoint_address = 0;
uint32_t watchpoint_length = 1;

// Set when a breakpoint or watchpoint stops the emulation.
uint8_t stop_condition_hit = 0;

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

#if (RVE_E_ZICSR == 1)
// Return the raw 32-bit value of a packed CSR struct.
static uint32_t csr_raw(const void *csr) {
    uint32_t value;
    memcpy(&value, csr, sizeof(value));
    return value;
}

// Return a human-readable name for a machine trap cause.
static const char *trap_cause_name(uint32_t code, uint8_t interrupt) {
    if (interrupt != 0) {
        switch (code) {
            case 3:
                return "machine software interrupt";
            case 7:
                return "machine timer interrupt";
            case 11:
                return "machine external interrupt";
            default:
                return "interrupt";
        }
    }
    switch (code) {
        case 0:
            return "instruction address misaligned";
        case 1:
            return "instruction access fault";
        case 2:
            return "illegal instruction";
        case 3:
            return "breakpoint";
        case 4:
            return "load address misaligned";
        case 5:
            return "load access fault";
        case 6:
            return "store/AMO address misaligned";
        case 7:
            return "store/AMO access fault";
        case 8:
            return "environment call from U-mode";
        case 9:
            return "environment call from S-mode";
        case 11:
            return "environment call from M-mode";
        case 12:
            return "instruction page fault";
        case 13:
            return "load page fault";
        case 15:
            return "store/AMO page fault";
        default:
            return "exception";
    }
}
#endif

// Print the machine CSRs and the key registers at a trap.
static void print_trap(const RiscvEmulatorState_t *state) {
#if (RVE_E_ZICSR == 1)
    uint32_t mcause = csr_raw(&state->csr.mcause);
    uint32_t mtvec = ((uint32_t)state->csr.mtvec.base << 2) | state->csr.mtvec.mode;
    printf("trap %zu: mcause=0x%08X (%s) mepc=0x%08X mtval=0x%08X mstatus=0x%08X mscratch=0x%08X mtvec=0x%08X\n",
           trap_count, mcause,
           trap_cause_name(state->csr.mcause.exceptioncode, state->csr.mcause.interrupt),
           state->csr.mepc, state->csr.mtval, csr_raw(&state->csr.mstatus),
           state->csr.mscratch.mscratch, mtvec);
    printf("  pc=0x%08X instruction=0x%08X ra=0x%08X sp=0x%08X gp=0x%08X tp=0x%08X\n",
           state->programcounter, state->instruction.value, state->reg.ra,
           state->reg.sp, state->reg.gp, state->reg.tp);
#else
    (void)state;
#endif
}

// Print all integer registers and the machine CSRs.
static void dump_registers(const RiscvEmulatorState_t *state) {
    static const char *const names[32] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
    for (int i = 0; i < 32; i++) {
        printf("x%-2d %-4s = 0x%08X\n", i, names[i], state->reg.x[i]);
    }
    printf("pc   = 0x%08X\n", state->programcounter);
#if (RVE_E_ZICSR == 1)
    printf("mstatus = 0x%08X mcause = 0x%08X mepc = 0x%08X mtval = 0x%08X\n",
           csr_raw(&state->csr.mstatus), csr_raw(&state->csr.mcause),
           state->csr.mepc, state->csr.mtval);
    printf("mtvec = 0x%08X mscratch = 0x%08X misa = 0x%08X\n",
           ((uint32_t)state->csr.mtvec.base << 2) | state->csr.mtvec.mode,
           state->csr.mscratch.mscratch, csr_raw(&state->csr.misa));
#endif
}

// Hexdump RAM bytes in 16-byte rows; addresses outside RAM print as ??.
static void dump_memory(uint32_t address, uint32_t length) {
    for (uint32_t offset = 0; offset < length; offset += 16) {
        uint32_t row = address + offset;
        printf("0x%08X:", row);
        for (uint32_t i = 0; i < 16 && offset + i < length; i++) {
            uint32_t current = row + i;
            if (current >= RAM_ORIGIN && current < RAM_ORIGIN + RAM_LENGTH) {
                printf(" %02X", memory[current - RAM_ORIGIN]);
            } else {
                printf(" ??");
            }
        }
        printf("\n");
    }
}

// Consumer hook: log traps and stop on a watchpoint.
void RiscvEmulatorHook(const RiscvEmulatorState_t *state, const RiscvEmulatorHookContext_t *context) {
    if (context->instruction != NULL && context->instruction[0] == '_') {
        if (trap_log != 0 && (trap_log_limit == 0 || trap_count < trap_log_limit)) {
            trap_count++;
            print_trap(state);
        }
        return;
    }

    if (watchpoint != 0 && context->hook == HOOK_BEGIN &&
        (context->length != 0 || context->memorylocation != 0)) {
        uint32_t access_length = (context->length != 0) ? (uint32_t)context->length : 4u;
        uint32_t address = context->memorylocation;
        if (address < watchpoint_address + watchpoint_length &&
            address + access_length > watchpoint_address) {
            printf("Watchpoint hit: %s at 0x%08X, pc=0x%08X, instruction=0x%08X\n",
                   context->instruction, address, state->programcounter,
                   state->instruction.value);
            stop_condition_hit = 1;
            pleasestop = 1;
        }
    }
}

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
    // instructions, -b <dtb> to load a device tree blob, -D <file> to attach
    // a disk image, -t [<count>] to log traps, -r to dump registers at stop,
    // -d <address>,<length> to dump RAM at stop, -s <address> to stop at a PC
    // and -w <address>[,<length>] to stop on a load or store. The binary path
    // is the last argument; run_test.sh passes it via elf2bin.sh.
    const char *binpath = NULL;
    const char *dtbpath = NULL;
    const char *diskpath = NULL;
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
        } else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            i++;
            diskpath = argv[i];
        } else if (strcmp(argv[i], "-t") == 0) {
            trap_log = 1;
            if (i + 1 < argc) {
                char *end;
                unsigned long count = strtoul(argv[i + 1], &end, 0);
                if (end != argv[i + 1] && *end == '\0') {
                    trap_log_limit = (size_t)count;
                    i++;
                }
            }
        } else if (strcmp(argv[i], "-r") == 0) {
            register_dump = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            i++;
            char *end;
            unsigned long address = strtoul(argv[i], &end, 0);
            if (*end != ',') {
                printf("Error: -d requires <address>,<length>, got '%s'.\n", argv[i]);
                return 2;
            }
            char *end2;
            unsigned long length = strtoul(end + 1, &end2, 0);
            if (*end2 != '\0') {
                printf("Error: -d requires <address>,<length>, got '%s'.\n", argv[i]);
                return 2;
            }
            memory_dump = 1;
            memory_dump_address = (uint32_t)address;
            memory_dump_length = (uint32_t)length;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            i++;
            char *end;
            unsigned long address = strtoul(argv[i], &end, 0);
            if (*end != '\0') {
                printf("Error: -s requires a numeric address, got '%s'.\n", argv[i]);
                return 2;
            }
            breakpoint = 1;
            breakpoint_address = (uint32_t)address;
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            i++;
            char *end;
            unsigned long address = strtoul(argv[i], &end, 0);
            unsigned long length = 1;
            if (*end == ',') {
                char *end2;
                length = strtoul(end + 1, &end2, 0);
                if (*end2 != '\0') {
                    printf("Error: -w requires <address>[,<length>], got '%s'.\n", argv[i]);
                    return 2;
                }
            } else if (*end != '\0') {
                printf("Error: -w requires <address>[,<length>], got '%s'.\n", argv[i]);
                return 2;
            }
            watchpoint = 1;
            watchpoint_address = (uint32_t)address;
            watchpoint_length = (uint32_t)length;
        } else {
            binpath = argv[i];
        }
    }

    if (binpath == NULL) {
        printf("Usage: %s [-v [<start>]] [-m <instructions>] [-p <interval>] [-b <dtb>] [-D <disk>] [-t [<count>]] [-r] [-d <address>,<length>] [-s <address>] [-w <address>[,<length>]] <binary>\n", argv[0]);
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

    // Open the optional disk image and attach it to virtio-blk.
    uint64_t disk_capacity = 0;
    if (diskpath != NULL) {
        disk_fd = open(diskpath, O_RDWR);
        if (disk_fd < 0) {
            free(memory);
            printf("file not found: %s\n", diskpath);
            return 2;
        }

        struct stat st;
        if (fstat(disk_fd, &st) != 0 || st.st_size <= 0 || (st.st_size % 512) != 0) {
            close(disk_fd);
            disk_fd = -1;
            free(memory);
            printf("Error: disk size must be a positive multiple of 512 bytes.\n");
            return 2;
        }

        disk_capacity = (uint64_t)st.st_size / 512u;
        if (verbose) {
            printf("Attached %s with %llu sectors.\n", diskpath, (unsigned long long)disk_capacity);
        }
    }

    virtio_blk_init(&virtio_blk, VIRTIO_BLK_ORIGIN, VIRTIO_BLK_SIZE, memory, RAM_ORIGIN, RAM_LENGTH, disk_capacity);

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
        if (breakpoint != 0 && RiscvEmulatorState.programcounter == breakpoint_address) {
            printf("Breakpoint hit at 0x%08X after %zu instructions.\n",
                   breakpoint_address, loopcounter);
            stop_condition_hit = 1;
            pleasestop = 1;
            break;
        }

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

        // If this prints then the instruction is not implemented.
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
    if (register_dump != 0 || stop_condition_hit != 0) {
        dump_registers(&RiscvEmulatorState);
    }
    if (memory_dump != 0) {
        dump_memory(memory_dump_address, memory_dump_length);
    }
    printf("Allocated %zu bytes of RAM (%zu bytes used by the binary).\n",
           (size_t)RAM_LENGTH, binsize);
    free(memory);

    if (disk_fd >= 0) {
        close(disk_fd);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

    if (testresult == TEST_PASS) {
        return 0;
    }
    return 1;
}