/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// The consumer provides its own RiscvEmulatorDisasmPrintf and
// RiscvEmulatorHook below; skip the library's weak definitions (they cannot
// be overridden in the same TU).
#define RVE_DISASM_PRINTF_OVERRIDE 1
#define RVE_HOOK_OVERRIDE          1

#include <RiscvEmulator.h>

#include "memory.h"

uint8_t *memory;
uint8_t ram_dirty[RAM_DIRTY_BYTES];

// Disk file descriptor, opened by -D <file>.
int disk_fd = -1;

// TAP file descriptor, opened by -N <ifname>.
int tap_fd = -1;

RiscvEmulatorState_t RiscvEmulatorState;

uint8_t pleasestop = 0;

// SIGUSR1 requests a snapshot. The handler only sets this flag; the main loop
// writes the files at an instruction boundary.
static const char *snapshot_prefix = NULL;
static const char *restore_prefix = NULL;
static volatile sig_atomic_t snapshot_requested = 0;
static FILE *snapshot_out;
static bool snapshot_first;

static bool stdin_pollable;
static bool tap_pollable;

// Idle statistics collected by the native board.
static uint64_t wfi_calls;
static uint64_t idle_ns;
static uint64_t longest_idle_ns;
static uint64_t wake_timer;
static uint64_t wake_stdin;
static uint64_t wake_tap;
static uint64_t wake_signal;
static uint64_t start_ns;

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

// Extra images loaded by -l <address>,<file>; repeatable.
#define MAX_LOADS 8
static struct {
    uint32_t address;
    const char *path;
} loads[MAX_LOADS];
static size_t load_count = 0;

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
            case 1:
                return "supervisor software interrupt";
            case 3:
                return "machine software interrupt";
            case 5:
                return "supervisor timer interrupt";
            case 7:
                return "machine timer interrupt";
            case 9:
                return "supervisor external interrupt";
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
#if (RVE_E_SV32 == 1)
    if (state->privilege == 1) {
        uint32_t scause = csr_raw(&state->csr.scause);
        uint32_t stvec = ((uint32_t)state->csr.stvec.base << 2) | state->csr.stvec.mode;
        printf("trap %zu: scause=0x%08X (%s) sepc=0x%08X stval=0x%08X sstatus=0x%08X sscratch=0x%08X stvec=0x%08X\n",
               trap_count, scause,
               trap_cause_name(state->csr.scause.exceptioncode, state->csr.scause.interrupt),
               state->csr.sepc, state->csr.stval,
               csr_raw(&state->csr.mstatus) & RVE_CSR_SSTATUS_READ_MASK,
               state->csr.sscratch, stvec);
        printf("  pc=0x%08X instruction=0x%08X ra=0x%08X sp=0x%08X gp=0x%08X tp=0x%08X\n",
               state->programcounter, state->instruction.value, state->reg.ra,
               state->reg.sp, state->reg.gp, state->reg.tp);
        return;
    }
#endif
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

/**
 * Sets the snapshot request flag from the SIGUSR1 handler.
 */
static void snapshot_signal_handler(int signal_number) {
    (void)signal_number;
    snapshot_requested = 1;
}

/**
 * Emits the key of the next flat JSON field.
 */
static void snapshot_key(const char *key) {
    if (!snapshot_first) {
        fputc(',', snapshot_out);
    }
    snapshot_first = false;
    fprintf(snapshot_out, "\"%s\":", key);
}

/**
 * Writes an 8-bit JSON field.
 */
static void snapshot_u8(const char *key, uint8_t value) {
    snapshot_key(key);
    fprintf(snapshot_out, "%u", value);
}

/**
 * Writes a 32-bit JSON field.
 */
static void snapshot_u32(const char *key, uint32_t value) {
    snapshot_key(key);
    fprintf(snapshot_out, "%u", value);
}

/**
 * Writes a 64-bit JSON field as a string so every digit survives a JSON parser.
 */
static void snapshot_u64(const char *key, uint64_t value) {
    snapshot_key(key);
    fprintf(snapshot_out, "\"%llu\"", (unsigned long long)value);
}

/**
 * Writes a boolean JSON field.
 */
static void snapshot_bool(const char *key, bool value) {
    snapshot_key(key);
    fputs(value ? "true" : "false", snapshot_out);
}

/**
 * Writes a byte array as a JSON array of numbers.
 */
static void snapshot_bytes(const char *key, const uint8_t *data, size_t length) {
    snapshot_key(key);
    fputc('[', snapshot_out);
    for (size_t i = 0; i < length; i++) {
        if (i != 0) {
            fputc(',', snapshot_out);
        }
        fprintf(snapshot_out, "%u", data[i]);
    }
    fputc(']', snapshot_out);
}

/**
 * Writes one prefixed 32-bit field of a virtio-mmio transport.
 */
static void snapshot_mmio_u32(const char *prefix, const char *field, uint32_t value) {
    char key[128];
    snprintf(key, sizeof(key), "%s.%s", prefix, field);
    snapshot_u32(key, value);
}

/**
 * Writes the restorable state of one virtio-mmio transport.
 */
static void snapshot_virtio_mmio(const char *prefix, const virtio_mmio_t *mmio) {
    snapshot_mmio_u32(prefix, "base", mmio->base);
    snapshot_mmio_u32(prefix, "size", mmio->size);
    snapshot_mmio_u32(prefix, "memory_base", mmio->memory_base);
    snapshot_mmio_u32(prefix, "memory_size", mmio->memory_size);
    snapshot_mmio_u32(prefix, "device_id", mmio->device_id);
    snapshot_mmio_u32(prefix, "vendor_id", mmio->vendor_id);
    snapshot_mmio_u32(prefix, "host_features", mmio->host_features);
    snapshot_mmio_u32(prefix, "host_features_sel", mmio->host_features_sel);
    snapshot_mmio_u32(prefix, "guest_features", mmio->guest_features);
    snapshot_mmio_u32(prefix, "guest_features_sel", mmio->guest_features_sel);
    snapshot_mmio_u32(prefix, "guest_page_size", mmio->guest_page_size);
    snapshot_mmio_u32(prefix, "queue_sel", mmio->queue_sel);
    snapshot_mmio_u32(prefix, "queue_count", mmio->queue_count);
    for (uint32_t i = 0; i < VIRTIO_MMIO_QUEUE_MAX; i++) {
        char key[128];
        const virtio_mmio_queue_t *queue = &mmio->queues[i];
        snprintf(key, sizeof(key), "%s.queue%u.queue_num", prefix, i);
        snapshot_u32(key, queue->queue_num);
        snprintf(key, sizeof(key), "%s.queue%u.queue_align", prefix, i);
        snapshot_u32(key, queue->queue_align);
        snprintf(key, sizeof(key), "%s.queue%u.queue_pfn", prefix, i);
        snapshot_u32(key, queue->queue_pfn);
        snprintf(key, sizeof(key), "%s.queue%u.last_avail_idx", prefix, i);
        snapshot_u32(key, queue->last_avail_idx);
        snprintf(key, sizeof(key), "%s.queue%u.used_idx", prefix, i);
        snapshot_u32(key, queue->used_idx);
    }
    snapshot_mmio_u32(prefix, "interrupt_status", mmio->interrupt_status);
    snapshot_mmio_u32(prefix, "status", mmio->status);
}

/**
 * Writes the CPU, CSR and device state to <prefix>.json and RAM to <prefix>.bin.
 */
static bool snapshot_write(const char *prefix) {
    char json_path[1024];
    char ram_path[1024];
    char json_tmp[1024];
    char ram_tmp[1024];
    if (snprintf(json_path, sizeof(json_path), "%s.json", prefix) >= (int)sizeof(json_path) ||
        snprintf(ram_path, sizeof(ram_path), "%s.bin", prefix) >= (int)sizeof(ram_path) ||
        snprintf(json_tmp, sizeof(json_tmp), "%s.json.tmp", prefix) >= (int)sizeof(json_tmp) ||
        snprintf(ram_tmp, sizeof(ram_tmp), "%s.bin.tmp", prefix) >= (int)sizeof(ram_tmp)) {
        printf("Snapshot path is too long: %s\n", prefix);
        return false;
    }

    FILE *json = fopen(json_tmp, "w");
    if (json == NULL) {
        printf("Cannot write %s.\n", json_tmp);
        return false;
    }
    FILE *ram = fopen(ram_tmp, "wb");
    if (ram == NULL) {
        printf("Cannot write %s.\n", ram_tmp);
        fclose(json);
        remove(json_tmp);
        return false;
    }

    snapshot_out = json;
    snapshot_first = true;
    fputc('{', json);

    snapshot_u32("magic", 0x52564553u);
    snapshot_u32("version", 1);
    snapshot_u64("instructions", (uint64_t)loopcounter);
    snapshot_u32("ram_origin", RAM_ORIGIN);
    snapshot_u32("ram_size", RAM_LENGTH);
    snapshot_u32("isa.m", RVE_E_M);
    snapshot_u32("isa.a", RVE_E_A);
    snapshot_u32("isa.c", RVE_E_C);
    snapshot_u32("isa.zicsr", RVE_E_ZICSR);
    snapshot_u32("isa.zicntr", RVE_E_ZICNTR);
    snapshot_u32("isa.sv32", RVE_E_SV32);
    snapshot_u32("isa.zalrsc", RVE_E_ZALRSC);

    snapshot_u32("cpu.pc", RiscvEmulatorState.programcounter);
    snapshot_u32("cpu.next_pc", RiscvEmulatorState.programcounternext);
    snapshot_u32("cpu.instruction", RiscvEmulatorState.instruction.value);
    snapshot_u8("cpu.instruction_executed", RiscvEmulatorState.instructionexecuted);
    snapshot_u8("trap.pending", RiscvEmulatorState.trap.pending);
    snapshot_u8("trap.cause", RiscvEmulatorState.trap.cause);
    snapshot_u32("trap.tval", RiscvEmulatorState.trap.tval);
    for (uint32_t i = 0; i < 32; i++) {
        char key[32];
        snprintf(key, sizeof(key), "cpu.x%u", i);
        snapshot_u32(key, RiscvEmulatorState.reg.x[i]);
    }
#if (RVE_E_ZALRSC == 1)
    snapshot_u8("cpu.reservation_valid", RiscvEmulatorState.reservationvalid);
    snapshot_u32("cpu.reservation_address", RiscvEmulatorState.reservationaddress);
#endif
#if (RVE_E_HOOK == 1 || RVE_E_DISASM == 1)
    snapshot_u8("cpu.instruction_handled", RiscvEmulatorState.instructionhandled);
#endif
#if (RVE_E_ZICSR == 1)
    snapshot_u8("cpu.privilege", RiscvEmulatorState.privilege);
    snapshot_u8("cpu.waiting", RiscvEmulatorState.waiting);
    snapshot_u8("cpu.wfi_timeout", RiscvEmulatorState.wfi_timeout);
#if (RVE_E_ZICNTR == 1)
    snapshot_u8("cpu.counter_written", RiscvEmulatorState.counterwritten);
#endif
    snapshot_u32("csr.mhartid", csr_raw(&RiscvEmulatorState.csr.mhartid));
    snapshot_u32("csr.marchid", RiscvEmulatorState.csr.marchid);
    snapshot_u32("csr.mstatus", csr_raw(&RiscvEmulatorState.csr.mstatus));
    snapshot_u32("csr.misa", csr_raw(&RiscvEmulatorState.csr.misa));
    snapshot_u32("csr.medeleg", csr_raw(&RiscvEmulatorState.csr.medeleg));
    snapshot_u32("csr.mideleg", csr_raw(&RiscvEmulatorState.csr.mideleg));
    snapshot_u32("csr.mie", csr_raw(&RiscvEmulatorState.csr.mie));
    snapshot_u32("csr.mtvec", csr_raw(&RiscvEmulatorState.csr.mtvec));
    snapshot_u32("csr.mstatush", csr_raw(&RiscvEmulatorState.csr.mstatush));
    snapshot_u32("csr.menvcfg", RiscvEmulatorState.csr.menvcfg);
    snapshot_u32("csr.mscratch", csr_raw(&RiscvEmulatorState.csr.mscratch));
    snapshot_u32("csr.mepc", RiscvEmulatorState.csr.mepc);
    snapshot_u32("csr.mcause", csr_raw(&RiscvEmulatorState.csr.mcause));
    snapshot_u32("csr.mtval", RiscvEmulatorState.csr.mtval);
    snapshot_u32("csr.mip", csr_raw(&RiscvEmulatorState.csr.mip));
    snapshot_u32("csr.mip_platform", RiscvEmulatorState.csr.mip_platform);
    snapshot_u32("csr.mip_platform_software", RiscvEmulatorState.csr.mip_platform_software);
    snapshot_u32("csr.pmpcfg0", csr_raw(&RiscvEmulatorState.csr.pmpcfg0));
    snapshot_u32("csr.pmpaddr0", csr_raw(&RiscvEmulatorState.csr.pmpaddr0));
    snapshot_u32("csr.mnstatus", csr_raw(&RiscvEmulatorState.csr.mnstatus));
    snapshot_u32("csr.stvec", csr_raw(&RiscvEmulatorState.csr.stvec));
    snapshot_u32("csr.scounteren", RiscvEmulatorState.csr.scounteren);
    snapshot_u32("csr.senvcfg", RiscvEmulatorState.csr.senvcfg);
    snapshot_u32("csr.sscratch", RiscvEmulatorState.csr.sscratch);
    snapshot_u32("csr.sepc", RiscvEmulatorState.csr.sepc);
    snapshot_u32("csr.scause", csr_raw(&RiscvEmulatorState.csr.scause));
    snapshot_u32("csr.stval", RiscvEmulatorState.csr.stval);
    snapshot_u32("csr.satp", csr_raw(&RiscvEmulatorState.csr.satp));
    snapshot_u32("csr.zeroreg", RiscvEmulatorState.csr.zeroreg);
    snapshot_u32("csr.mcounteren", RiscvEmulatorState.csr.mcounteren);
#if (RVE_E_ZICNTR == 1)
    snapshot_u64("csr.cycle", RiscvEmulatorState.csr.cycle);
    snapshot_u64("csr.time", RiscvEmulatorState.csr.time);
    snapshot_u64("csr.instret", RiscvEmulatorState.csr.instret);
    snapshot_u64("csr.mhpmcounter3", RiscvEmulatorState.csr.mhpmcounter3);
#endif
#endif

    snapshot_u64("clint.mtime", clint.mtime);
    snapshot_u64("clint.mtimecmp", clint.mtimecmp);
    snapshot_u64("clint.last_us", clint.last_us);
    snapshot_u32("clint.msip", clint.msip);
    snapshot_u32("clint.mtime_high_latched", clint.mtime_high_latched);
    snapshot_bool("clint.mtime_high_latch_valid", clint.mtime_high_latch_valid);

    for (uint32_t i = 0; i < PLIC_SOURCE_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "plic.priority%u", i);
        snapshot_u32(key, plic.priority[i]);
    }
    snapshot_u32("plic.pending", plic.pending);
    for (uint32_t i = 0; i < PLIC_CONTEXT_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "plic.enable%u", i);
        snapshot_u32(key, plic.enable[i]);
        snprintf(key, sizeof(key), "plic.threshold%u", i);
        snapshot_u32(key, plic.threshold[i]);
    }

    snapshot_u8("uart.ier", uart.ier);
    snapshot_u8("uart.fcr", uart.fcr);
    snapshot_u8("uart.lcr", uart.lcr);
    snapshot_u8("uart.mcr", uart.mcr);
    snapshot_u8("uart.scr", uart.scr);
    snapshot_u8("uart.dll", uart.dll);
    snapshot_u8("uart.dlm", uart.dlm);
    snapshot_bytes("uart.fifo", uart.fifo, UART16550_FIFO_SIZE);
    snapshot_u8("uart.fifo_read", uart.fifo_read);
    snapshot_u8("uart.fifo_write", uart.fifo_write);
    snapshot_u8("uart.fifo_count", uart.fifo_count);
    snapshot_bool("uart.overrun", uart.overrun);
    snapshot_bool("uart.thre_pending", uart.thre_pending);

    snapshot_virtio_mmio("virtio_blk", &virtio_blk.mmio);
    snapshot_u64("virtio_blk.capacity", virtio_blk.capacity);
    snapshot_virtio_mmio("virtio_console", &virtio_console.mmio);
    snapshot_bytes("virtio_console.rx_fifo", virtio_console.rx_fifo, VIRTIO_CONSOLE_RX_FIFO_SIZE);
    snapshot_u8("virtio_console.rx_read", virtio_console.rx_read);
    snapshot_u8("virtio_console.rx_write", virtio_console.rx_write);
    snapshot_u8("virtio_console.rx_count", virtio_console.rx_count);
    snapshot_bool("virtio_console.rx_overrun", virtio_console.rx_overrun);
    snapshot_virtio_mmio("virtio_net", &virtio_net.mmio);
    snapshot_virtio_mmio("virtio_rng", &virtio_rng.mmio);

    snapshot_u64("rtc.base_ns", goldfish_rtc.base_ns);
    snapshot_u64("rtc.anchor_ns", goldfish_rtc.anchor_ns);
    snapshot_u32("rtc.latch_high", goldfish_rtc.latch_high);
    snapshot_u32("rtc.alarm_low", goldfish_rtc.alarm_low);
    snapshot_u32("rtc.alarm_high", goldfish_rtc.alarm_high);
    snapshot_u32("rtc.irq_enabled", goldfish_rtc.irq_enabled);
    snapshot_u32("rtc.alarm_status", goldfish_rtc.alarm_status);
    snapshot_u32("sig.pending", sig_pending);

    fputs("}\n", json);
    if (fclose(json) != 0) {
        printf("Cannot close %s.\n", json_tmp);
        fclose(ram);
        remove(json_tmp);
        remove(ram_tmp);
        return false;
    }
    if (fwrite(memory, 1, RAM_LENGTH, ram) != RAM_LENGTH || fclose(ram) != 0) {
        printf("Cannot write %s.\n", ram_tmp);
        remove(json_tmp);
        remove(ram_tmp);
        return false;
    }
    bool json_installed = rename(json_tmp, json_path) == 0;
    bool ram_installed = rename(ram_tmp, ram_path) == 0;
    if (!json_installed || !ram_installed) {
        if (json_installed) {
            remove(json_path);
        }
        if (ram_installed) {
            remove(ram_path);
        }
        remove(json_tmp);
        remove(ram_tmp);
        printf("Cannot install snapshot files for %s.\n", prefix);
        return false;
    }
    printf("Snapshot written to %s and %s after %zu instructions.\n", json_path, ram_path, loopcounter);
    return true;
}

/**
 * Returns the value text that follows a flat JSON key.
 */
static const char *json_value(const char *json, const char *key) {
    char pattern[128];
    int length = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (length < 0 || (size_t)length >= sizeof(pattern)) {
        return NULL;
    }
    const char *found = strstr(json, pattern);
    if (found == NULL) {
        return NULL;
    }
    return found + strlen(pattern);
}

/**
 * Reads a decimal 32-bit JSON field.
 */
static bool json_u32(const char *json, const char *key, uint32_t *value) {
    const char *text = json_value(json, key);
    if (text == NULL) {
        return false;
    }
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (end == text) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

/**
 * Reads a decimal 8-bit JSON field.
 */
static bool json_u8(const char *json, const char *key, uint8_t *value) {
    uint32_t parsed;
    if (!json_u32(json, key, &parsed) || parsed > UINT8_MAX) {
        return false;
    }
    *value = (uint8_t)parsed;
    return true;
}

/**
 * Reads a 64-bit JSON field stored as a decimal string.
 */
static bool json_u64(const char *json, const char *key, uint64_t *value) {
    const char *text = json_value(json, key);
    if (text == NULL || *text != '"') {
        return false;
    }
    char *end;
    unsigned long long parsed = strtoull(text + 1, &end, 10);
    if (end == text + 1 || *end != '"') {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

/**
 * Reads a boolean JSON field.
 */
static bool json_bool(const char *json, const char *key, bool *value) {
    const char *text = json_value(json, key);
    if (text == NULL) {
        return false;
    }
    if (strncmp(text, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (strncmp(text, "false", 5) == 0) {
        *value = false;
        return true;
    }
    return false;
}

/**
 * Reads a JSON array of byte values.
 */
static bool json_bytes(const char *json, const char *key, uint8_t *bytes, size_t length) {
    const char *text = json_value(json, key);
    if (text == NULL || *text != '[') {
        return false;
    }
    text++;
    for (size_t i = 0; i < length; i++) {
        while (*text == ' ' || *text == ',') {
            text++;
        }
        char *end;
        unsigned long parsed = strtoul(text, &end, 10);
        if (end == text || parsed > UINT8_MAX) {
            return false;
        }
        bytes[i] = (uint8_t)parsed;
        text = end;
    }
    return true;
}

/**
 * Writes a 32-bit value into a packed CSR field.
 */
#if (RVE_E_ZICSR == 1)
/**
 * Writes a 32-bit value into a packed CSR field by byte offset.
 */
static void csr_set_u32(size_t offset, uint32_t value) {
    memcpy((uint8_t *)&RiscvEmulatorState.csr + offset, &value, sizeof(value));
}

#if (RVE_E_ZICNTR == 1)
/**
 * Writes a 64-bit value into a packed CSR field by byte offset.
 */
static void csr_set_u64(size_t offset, uint64_t value) {
    memcpy((uint8_t *)&RiscvEmulatorState.csr + offset, &value, sizeof(value));
}
#endif
#endif

/**
 * Restores one virtio-mmio transport from flat JSON keys.
 */
static bool restore_mmio(const char *json, const char *prefix, virtio_mmio_t *mmio) {
    char key[128];
    bool ok = true;
#define RESTORE_MMIO_U32(field)                              \
    do {                                                     \
        snprintf(key, sizeof(key), "%s.%s", prefix, #field); \
        ok = json_u32(json, key, &mmio->field) && ok;        \
    } while (0)
    RESTORE_MMIO_U32(base);
    RESTORE_MMIO_U32(size);
    RESTORE_MMIO_U32(memory_base);
    RESTORE_MMIO_U32(memory_size);
    RESTORE_MMIO_U32(device_id);
    RESTORE_MMIO_U32(vendor_id);
    RESTORE_MMIO_U32(host_features);
    RESTORE_MMIO_U32(host_features_sel);
    RESTORE_MMIO_U32(guest_features);
    RESTORE_MMIO_U32(guest_features_sel);
    RESTORE_MMIO_U32(guest_page_size);
    RESTORE_MMIO_U32(queue_sel);
    RESTORE_MMIO_U32(queue_count);
    for (uint32_t i = 0; i < VIRTIO_MMIO_QUEUE_MAX; i++) {
        virtio_mmio_queue_t *queue = &mmio->queues[i];
        snprintf(key, sizeof(key), "%s.queue%u.queue_num", prefix, i);
        ok = json_u32(json, key, &queue->queue_num) && ok;
        snprintf(key, sizeof(key), "%s.queue%u.queue_align", prefix, i);
        ok = json_u32(json, key, &queue->queue_align) && ok;
        snprintf(key, sizeof(key), "%s.queue%u.queue_pfn", prefix, i);
        ok = json_u32(json, key, &queue->queue_pfn) && ok;
        snprintf(key, sizeof(key), "%s.queue%u.last_avail_idx", prefix, i);
        uint32_t value;
        ok = json_u32(json, key, &value) && ok;
        queue->last_avail_idx = (uint16_t)value;
        snprintf(key, sizeof(key), "%s.queue%u.used_idx", prefix, i);
        ok = json_u32(json, key, &value) && ok;
        queue->used_idx = (uint16_t)value;
    }
    RESTORE_MMIO_U32(interrupt_status);
    RESTORE_MMIO_U32(status);
#undef RESTORE_MMIO_U32
    return ok;
}

/**
 * Restores CPU, CSR, device and RAM state from a snapshot.
 */
static bool snapshot_restore(const char *prefix) {
    char json_path[1024];
    char ram_path[1024];
    if (snprintf(json_path, sizeof(json_path), "%s.json", prefix) >= (int)sizeof(json_path) ||
        snprintf(ram_path, sizeof(ram_path), "%s.bin", prefix) >= (int)sizeof(ram_path)) {
        printf("Snapshot path is too long: %s\n", prefix);
        return false;
    }

    FILE *json = fopen(json_path, "r");
    if (json == NULL) {
        printf("Cannot read %s.\n", json_path);
        return false;
    }
    if (fseek(json, 0, SEEK_END) != 0) {
        fclose(json);
        return false;
    }
    long size = ftell(json);
    if (size <= 0 || size > 1024 * 1024 || fseek(json, 0, SEEK_SET) != 0) {
        fclose(json);
        return false;
    }
    char *text = malloc((size_t)size + 1u);
    if (text == NULL || fread(text, 1, (size_t)size, json) != (size_t)size) {
        free(text);
        fclose(json);
        return false;
    }
    text[size] = '\0';
    fclose(json);

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t ram_origin = 0;
    uint32_t ram_size = 0;
    bool ok = json_u32(text, "magic", &magic) && magic == 0x52564553u &&
              json_u32(text, "version", &version) && version == 1 &&
              json_u32(text, "ram_origin", &ram_origin) && ram_origin == RAM_ORIGIN &&
              json_u32(text, "ram_size", &ram_size) && ram_size == RAM_LENGTH;
    if (!ok) {
        printf("Snapshot %s has an incompatible header.\n", json_path);
        free(text);
        return false;
    }
    uint32_t isa_m = 0;
    uint32_t isa_a = 0;
    uint32_t isa_c = 0;
    uint32_t isa_zicsr = 0;
    uint32_t isa_zicntr = 0;
    uint32_t isa_sv32 = 0;
    uint32_t isa_zalrsc = 0;
    ok = json_u32(text, "isa.m", &isa_m) && isa_m == RVE_E_M &&
         json_u32(text, "isa.a", &isa_a) && isa_a == RVE_E_A &&
         json_u32(text, "isa.c", &isa_c) && isa_c == RVE_E_C &&
         json_u32(text, "isa.zicsr", &isa_zicsr) && isa_zicsr == RVE_E_ZICSR &&
         json_u32(text, "isa.zicntr", &isa_zicntr) && isa_zicntr == RVE_E_ZICNTR &&
         json_u32(text, "isa.sv32", &isa_sv32) && isa_sv32 == RVE_E_SV32 &&
         json_u32(text, "isa.zalrsc", &isa_zalrsc) && isa_zalrsc == RVE_E_ZALRSC;
    if (!ok) {
        printf("Snapshot %s was written with a different ISA configuration.\n", json_path);
        free(text);
        return false;
    }

    uint64_t instructions = 0;
    ok = json_u64(text, "instructions", &instructions) && ok;
    loopcounter = (size_t)instructions;
    ok = json_u32(text, "cpu.pc", &RiscvEmulatorState.programcounter) && ok;
    ok = json_u32(text, "cpu.next_pc", &RiscvEmulatorState.programcounternext) && ok;
    ok = json_u32(text, "cpu.instruction", &RiscvEmulatorState.instruction.value) && ok;
    ok = json_u8(text, "cpu.instruction_executed", &RiscvEmulatorState.instructionexecuted) && ok;
    ok = json_u8(text, "trap.pending", &RiscvEmulatorState.trap.pending) && ok;
    ok = json_u8(text, "trap.cause", &RiscvEmulatorState.trap.cause) && ok;
    ok = json_u32(text, "trap.tval", &RiscvEmulatorState.trap.tval) && ok;
    for (uint32_t i = 0; i < 32; i++) {
        char key[32];
        snprintf(key, sizeof(key), "cpu.x%u", i);
        ok = json_u32(text, key, &RiscvEmulatorState.reg.x[i]) && ok;
    }
#if (RVE_E_ZALRSC == 1)
    ok = json_u8(text, "cpu.reservation_valid", &RiscvEmulatorState.reservationvalid) && ok;
    ok = json_u32(text, "cpu.reservation_address", &RiscvEmulatorState.reservationaddress) && ok;
#endif
#if (RVE_E_HOOK == 1 || RVE_E_DISASM == 1)
    ok = json_u8(text, "cpu.instruction_handled", &RiscvEmulatorState.instructionhandled) && ok;
#endif
#if (RVE_E_ZICSR == 1)
    ok = json_u8(text, "cpu.privilege", &RiscvEmulatorState.privilege) && ok;
    ok = json_u8(text, "cpu.waiting", &RiscvEmulatorState.waiting) && ok;
    ok = json_u8(text, "cpu.wfi_timeout", &RiscvEmulatorState.wfi_timeout) && ok;
#if (RVE_E_ZICNTR == 1)
    ok = json_u8(text, "cpu.counter_written", &RiscvEmulatorState.counterwritten) && ok;
#endif
#define RESTORE_CSR(field)                                \
    do {                                                  \
        uint32_t value;                                   \
        ok = json_u32(text, "csr." #field, &value) && ok; \
        csr_set_u32(offsetof(RiscvCSR_t, field), value);  \
    } while (0)
    RESTORE_CSR(mhartid);
    RESTORE_CSR(marchid);
    RESTORE_CSR(mstatus);
    RESTORE_CSR(misa);
    RESTORE_CSR(medeleg);
    RESTORE_CSR(mideleg);
    RESTORE_CSR(mie);
    RESTORE_CSR(mtvec);
    RESTORE_CSR(mstatush);
    RESTORE_CSR(menvcfg);
    RESTORE_CSR(mscratch);
    RESTORE_CSR(mepc);
    RESTORE_CSR(mcause);
    RESTORE_CSR(mtval);
    RESTORE_CSR(mip);
    RESTORE_CSR(mip_platform);
    RESTORE_CSR(mip_platform_software);
    RESTORE_CSR(pmpcfg0);
    RESTORE_CSR(pmpaddr0);
    RESTORE_CSR(mnstatus);
    RESTORE_CSR(stvec);
    RESTORE_CSR(scounteren);
    RESTORE_CSR(senvcfg);
    RESTORE_CSR(sscratch);
    RESTORE_CSR(sepc);
    RESTORE_CSR(scause);
    RESTORE_CSR(stval);
    RESTORE_CSR(satp);
    RESTORE_CSR(zeroreg);
    RESTORE_CSR(mcounteren);
#undef RESTORE_CSR
#if (RVE_E_ZICNTR == 1)
#define RESTORE_CSR64(field)                              \
    do {                                                  \
        uint64_t value;                                   \
        ok = json_u64(text, "csr." #field, &value) && ok; \
        csr_set_u64(offsetof(RiscvCSR_t, field), value);  \
    } while (0)
    RESTORE_CSR64(cycle);
    RESTORE_CSR64(time);
    RESTORE_CSR64(instret);
    RESTORE_CSR64(mhpmcounter3);
#undef RESTORE_CSR64
#endif
#endif

    ok = json_u64(text, "clint.mtime", &clint.mtime) && ok;
    ok = json_u64(text, "clint.mtimecmp", &clint.mtimecmp) && ok;
    ok = json_u64(text, "clint.last_us", &clint.last_us) && ok;
    ok = json_u32(text, "clint.msip", &clint.msip) && ok;
    ok = json_u32(text, "clint.mtime_high_latched", &clint.mtime_high_latched) && ok;
    ok = json_bool(text, "clint.mtime_high_latch_valid", &clint.mtime_high_latch_valid) && ok;
    for (uint32_t i = 0; i < PLIC_SOURCE_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "plic.priority%u", i);
        ok = json_u32(text, key, &plic.priority[i]) && ok;
    }
    ok = json_u32(text, "plic.pending", &plic.pending) && ok;
    for (uint32_t i = 0; i < PLIC_CONTEXT_COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "plic.enable%u", i);
        ok = json_u32(text, key, &plic.enable[i]) && ok;
        snprintf(key, sizeof(key), "plic.threshold%u", i);
        ok = json_u32(text, key, &plic.threshold[i]) && ok;
    }
    ok = json_u8(text, "uart.ier", &uart.ier) && ok;
    ok = json_u8(text, "uart.fcr", &uart.fcr) && ok;
    ok = json_u8(text, "uart.lcr", &uart.lcr) && ok;
    ok = json_u8(text, "uart.mcr", &uart.mcr) && ok;
    ok = json_u8(text, "uart.scr", &uart.scr) && ok;
    ok = json_u8(text, "uart.dll", &uart.dll) && ok;
    ok = json_u8(text, "uart.dlm", &uart.dlm) && ok;
    ok = json_bytes(text, "uart.fifo", uart.fifo, UART16550_FIFO_SIZE) && ok;
    ok = json_u8(text, "uart.fifo_read", &uart.fifo_read) && ok;
    ok = json_u8(text, "uart.fifo_write", &uart.fifo_write) && ok;
    ok = json_u8(text, "uart.fifo_count", &uart.fifo_count) && ok;
    ok = json_bool(text, "uart.overrun", &uart.overrun) && ok;
    ok = json_bool(text, "uart.thre_pending", &uart.thre_pending) && ok;
    ok = restore_mmio(text, "virtio_blk", &virtio_blk.mmio) && ok;
    ok = json_u64(text, "virtio_blk.capacity", &virtio_blk.capacity) && ok;
    ok = restore_mmio(text, "virtio_console", &virtio_console.mmio) && ok;
    ok = json_bytes(text, "virtio_console.rx_fifo", virtio_console.rx_fifo, VIRTIO_CONSOLE_RX_FIFO_SIZE) && ok;
    ok = json_u8(text, "virtio_console.rx_read", &virtio_console.rx_read) && ok;
    ok = json_u8(text, "virtio_console.rx_write", &virtio_console.rx_write) && ok;
    ok = json_u8(text, "virtio_console.rx_count", &virtio_console.rx_count) && ok;
    ok = json_bool(text, "virtio_console.rx_overrun", &virtio_console.rx_overrun) && ok;
    ok = restore_mmio(text, "virtio_net", &virtio_net.mmio) && ok;
    ok = restore_mmio(text, "virtio_rng", &virtio_rng.mmio) && ok;
    ok = json_u64(text, "rtc.base_ns", &goldfish_rtc.base_ns) && ok;
    ok = json_u64(text, "rtc.anchor_ns", &goldfish_rtc.anchor_ns) && ok;
    ok = json_u32(text, "rtc.latch_high", &goldfish_rtc.latch_high) && ok;
    ok = json_u32(text, "rtc.alarm_low", &goldfish_rtc.alarm_low) && ok;
    ok = json_u32(text, "rtc.alarm_high", &goldfish_rtc.alarm_high) && ok;
    ok = json_u32(text, "rtc.irq_enabled", &goldfish_rtc.irq_enabled) && ok;
    ok = json_u32(text, "rtc.alarm_status", &goldfish_rtc.alarm_status) && ok;
    ok = json_u32(text, "sig.pending", &sig_pending) && ok;
    free(text);
    if (!ok) {
        printf("Snapshot %s is missing state fields.\n", json_path);
        return false;
    }

    FILE *ram = fopen(ram_path, "rb");
    if (ram == NULL) {
        printf("Cannot read %s.\n", ram_path);
        return false;
    }
    size_t got = fread(memory, 1, RAM_LENGTH, ram);
    fclose(ram);
    if (got != RAM_LENGTH) {
        printf("Snapshot RAM %s has %zu bytes, expected %u.\n", ram_path, got, RAM_LENGTH);
        return false;
    }

    // The saved host clock belongs to the process that wrote the snapshot.
    // Re-anchor both time sources to this host so the next tick adds only the
    // time elapsed since the restore.
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    clint.last_us = (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
    goldfish_rtc.anchor_ns = (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
    printf("Restored snapshot %s at %zu instructions, pc=0x%08X.\n",
           prefix, loopcounter, RiscvEmulatorState.programcounter);
    return true;
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

// Drain all currently available stdin bytes into the emulated UART and console.
static void poll_stdin(void) {
    uint8_t ch;
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        uart16550_rx_push(&uart, ch);
        virtio_console_rx_push(&virtio_console, ch);
    }
}

// Drain all currently available TAP frames into the emulated network device.
// The descriptor is nonblocking, but poll() with a zero timeout keeps the main
// loop from entering read() when no frame is queued.
static void poll_tap(void) {
    uint8_t frame[VIRTIO_NET_BUFFER_SIZE];
    ssize_t length;

    if (tap_fd < 0) {
        return;
    }

    struct pollfd pfd = {.fd = tap_fd, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 0) <= 0) {
        return;
    }

    while ((length = read(tap_fd, frame, sizeof(frame))) > 0) {
        virtio_net_rx_push(&virtio_net, frame, (uint32_t)length);
    }
}

// A programmed CLINT timer may sleep up to one second. Without a timer the
// wait stays at one millisecond so the ACT timeout tests remain bounded.
#define WFI_TIMER_SLEEP_CAP_US 1000000u
#define WFI_NO_TIMER_SLEEP_US  1000u

// Returns true when stdin can wake ppoll(). Regular files are always ready.
static bool stdin_is_pollable(void) {
    struct stat st;
    if (fstat(STDIN_FILENO, &st) != 0) {
        return false;
    }
    return isatty(STDIN_FILENO) != 0 || S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode);
}

// Returns the monotonic host clock in nanoseconds.
static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

// Prints a duration in the format used by the Linux time command.
static void print_time(const char *label, uint64_t ns, const char *note) {
    uint64_t milliseconds = ns / 1000000u;
    printf("%-4s %llum%llu.%03llus%s\n", label,
           (unsigned long long)(milliseconds / 60000u),
           (unsigned long long)((milliseconds / 1000u) % 60u),
           (unsigned long long)(milliseconds % 1000u),
           note);
}

// Blocks the host until the CLINT deadline or an input event wakes the hart.
static void board_idle(void) {
    poll_stdin();
    poll_tap();
    wfi_calls++;

    struct pollfd fds[2] = {0};
    nfds_t nfds = 0;
    int stdin_index = -1;
    int tap_index = -1;

    if (stdin_pollable) {
        stdin_index = (int)nfds;
        fds[nfds].fd = STDIN_FILENO;
        fds[nfds].events = POLLIN;
        nfds++;
    }
    if (tap_fd >= 0 && tap_pollable) {
        tap_index = (int)nfds;
        fds[nfds].fd = tap_fd;
        fds[nfds].events = POLLIN;
        nfds++;
    }

    uint64_t begin = monotonic_ns();
    bool interrupted = false;

    for (;;) {
        uint64_t sleep_us = WFI_NO_TIMER_SLEEP_US;
        if (clint.mtimecmp > clint.mtime) {
            uint64_t remaining_us = clint.mtimecmp - clint.mtime;
            sleep_us = remaining_us < WFI_TIMER_SLEEP_CAP_US ? remaining_us : WFI_TIMER_SLEEP_CAP_US;
        }

        struct timespec timeout = {
            .tv_sec = (time_t)(sleep_us / 1000000u),
            .tv_nsec = (long)((sleep_us % 1000000u) * 1000u),
        };

        int result = ppoll(fds, nfds, &timeout, NULL);
        if (result >= 0 || errno != EINTR) {
            break;
        }
        interrupted = true;
    }

    uint64_t slept = monotonic_ns() - begin;
    idle_ns += slept;
    if (slept > longest_idle_ns) {
        longest_idle_ns = slept;
    }

    if (interrupted) {
        wake_signal++;
    } else if (stdin_index >= 0 && fds[stdin_index].revents != 0) {
        wake_stdin++;
    } else if (tap_index >= 0 && fds[tap_index].revents != 0) {
        wake_tap++;
    } else {
        wake_timer++;
    }

    if (stdin_index >= 0) {
        short revents = fds[stdin_index].revents;
        if ((revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            stdin_pollable = false;
        } else if ((revents & POLLIN) != 0) {
            poll_stdin();
        }
    }
    if (tap_index >= 0) {
        short revents = fds[tap_index].revents;
        if ((revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            tap_pollable = false;
        } else if ((revents & POLLIN) != 0) {
            poll_tap();
        }
    }
}

int main(int argc, char *argv[]) {
    start_ns = monotonic_ns();

    // Parse -v [<start>] to enable verbose instruction tracing from an
    // instruction count, -m <N> to limit the number of simulated instructions
    // (0 = unlimited, the default), -p <N> to print the PC every N
    // instructions, -b <dtb> to load a device tree blob, -l <address>,<file>
    // to load an extra image, -e <address> to set the initial PC, -D <file>
    // to attach a disk image, -N <ifname> to attach a TAP interface,
    // -t [<count>] to log traps, -r to dump registers at stop,
    // -d <address>,<length> to dump RAM at stop, -s <address> to stop at a PC,
    // -w <address>[,<length>] to stop on a load or store, -S <prefix> to write
    // a snapshot on SIGUSR1 and -R <prefix> to restore one. The binary path is
    // the last argument; run_test.sh passes it via elf2bin.sh.
    const char *binpath = NULL;
    const char *dtbpath = NULL;
    const char *diskpath = NULL;
    const char *tappath = NULL;
    uint32_t entry = RAM_ORIGIN;
    uint32_t dtb_address_override = 0;
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
            char *end;
            unsigned long address = strtoul(argv[i], &end, 0);
            if (*end == ',') {
                dtb_address_override = (uint32_t)address;
                dtbpath = end + 1;
            } else {
                dtbpath = argv[i];
            }
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            i++;
            char *end;
            unsigned long address = strtoul(argv[i], &end, 0);
            if (*end != ',' || end[1] == '\0') {
                printf("Error: -l requires <address>,<file>, got '%s'.\n", argv[i]);
                return 2;
            }
            if (load_count == MAX_LOADS) {
                printf("Error: at most %d -l options are supported.\n", MAX_LOADS);
                return 2;
            }
            loads[load_count].address = (uint32_t)address;
            loads[load_count].path = end + 1;
            load_count++;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            i++;
            char *end;
            unsigned long address = strtoul(argv[i], &end, 0);
            if (*end != '\0') {
                printf("Error: -e requires a numeric address, got '%s'.\n", argv[i]);
                return 2;
            }
            entry = (uint32_t)address;
        } else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            i++;
            diskpath = argv[i];
        } else if (strcmp(argv[i], "-N") == 0 && i + 1 < argc) {
            i++;
            tappath = argv[i];
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            i++;
            snapshot_prefix = argv[i];
        } else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc) {
            i++;
            restore_prefix = argv[i];
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

    if (binpath == NULL && load_count == 0 && restore_prefix == NULL) {
        printf("Usage: %s [-v [<start>]] [-m <instructions>] [-p <interval>] [-b <dtb>] [-l <address>,<file>] [-e <address>] [-D <disk>] [-N <ifname>] [-S <prefix>] [-R <prefix>] [-t [<count>]] [-r] [-d <address>,<length>] [-s <address>] [-w <address>[,<length>]] [<binary>]\n", argv[0]);
        return 2;
    }

    // Unbuffered stdout so the RVCP-SUMMARY line is not lost if the emulator halts.
    setvbuf(stdout, NULL, _IONBF, 0);
    pleasestop = 0;

    if (snapshot_prefix != NULL) {
        struct sigaction action = {0};
        action.sa_handler = snapshot_signal_handler;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGUSR1, &action, NULL) != 0) {
            printf("Cannot install the SIGUSR1 handler.\n");
            return 2;
        }
    }

    memory = malloc(RAM_LENGTH);
    if (memory == NULL) {
        printf("Failed to allocate %zu bytes of RAM.\n", (size_t)RAM_LENGTH);
        return 2;
    }
    memset(memory, 0, RAM_LENGTH);

    // Load the actual binary (objcopy -O binary output) directly into RAM.
    // Its byte 0 corresponds to address 0x80000000 (RAM_ORIGIN).
    size_t binsize = 0;
    size_t loaded_bytes = 0;
    if (binpath != NULL) {
        FILE *fbin = fopen(binpath, "rb");
        if (fbin == NULL) {
            free(memory);
            printf("file not found: %s\n", binpath);
            return 2;
        }
        binsize = fread(memory, sizeof(uint8_t), RAM_LENGTH, fbin);
        if (ferror(fbin)) {
            printf("Error reading %s.\n", binpath);
            fclose(fbin);
            free(memory);
            return 2;
        }
        loaded_bytes = binsize;
        ram_mark_dirty(RAM_ORIGIN, (uint32_t)binsize);
        if (verbose) {
            printf("Read %zu bytes.\n", binsize);
        }
        fclose(fbin);
    }

    // Load the extra images from -l <address>,<file>.
    for (size_t i = 0; i < load_count; i++) {
        if (loads[i].address < RAM_ORIGIN || loads[i].address >= RAM_ORIGIN + RAM_LENGTH) {
            free(memory);
            printf("Error: -l address 0x%08X is outside RAM.\n", loads[i].address);
            return 2;
        }
        FILE *fload = fopen(loads[i].path, "rb");
        if (fload == NULL) {
            free(memory);
            printf("file not found: %s\n", loads[i].path);
            return 2;
        }
        size_t offset = loads[i].address - RAM_ORIGIN;
        size_t size = fread(&memory[offset], sizeof(uint8_t), RAM_LENGTH - offset, fload);
        if (ferror(fload)) {
            fclose(fload);
            free(memory);
            printf("Error reading %s.\n", loads[i].path);
            return 2;
        }
        loaded_bytes += size;
        ram_mark_dirty(loads[i].address, (uint32_t)size);
        fclose(fload);
        if (verbose) {
            printf("Loaded %zu bytes at 0x%08X.\n", size, loads[i].address);
        }
    }

    // Load the device tree blob. Without an address it goes into the reserved
    // area at the top of RAM; -b <address>,<file> puts it at the given address.
    // The board passes its physical address to the kernel in a1.
    uint32_t dtb_address = 0;
    if (dtbpath != NULL) {
        FILE *fdtb = fopen(dtbpath, "rb");
        if (fdtb == NULL) {
            free(memory);
            printf("file not found: %s\n", dtbpath);
            return 2;
        }
        fseek(fdtb, 0, SEEK_END);
        long dtbsize = ftell(fdtb);
        fseek(fdtb, 0, SEEK_SET);
        if (dtbsize <= 0) {
            fclose(fdtb);
            free(memory);
            printf("Error: DTB is empty.\n");
            return 2;
        }
        size_t dtb_offset;
        if (dtb_address_override != 0) {
            if (dtb_address_override < RAM_ORIGIN || dtb_address_override >= RAM_ORIGIN + RAM_LENGTH) {
                fclose(fdtb);
                free(memory);
                printf("Error: -b address 0x%08X is outside RAM.\n", dtb_address_override);
                return 2;
            }
            dtb_offset = dtb_address_override - RAM_ORIGIN;
            if ((size_t)dtbsize > (size_t)RAM_LENGTH - dtb_offset) {
                fclose(fdtb);
                free(memory);
                printf("Error: DTB does not fit at 0x%08X.\n", dtb_address_override);
                return 2;
            }
        } else {
            if (binsize > (size_t)RAM_LENGTH - (size_t)DTB_RESERVE) {
                fclose(fdtb);
                free(memory);
                printf("Error: binary overlaps the DTB reserve.\n");
                return 2;
            }
            if ((size_t)dtbsize > (size_t)DTB_RESERVE) {
                fclose(fdtb);
                free(memory);
                printf("Error: DTB must be between 1 and %u bytes.\n", (unsigned)DTB_RESERVE);
                return 2;
            }
            dtb_offset = (size_t)RAM_LENGTH - (size_t)DTB_RESERVE;
        }
        if (fread(&memory[dtb_offset], (size_t)dtbsize, 1, fdtb) != 1) {
            fclose(fdtb);
            free(memory);
            printf("Error reading %s.\n", dtbpath);
            return 2;
        }
        loaded_bytes += (size_t)dtbsize;
        ram_mark_dirty((uint32_t)(RAM_ORIGIN + dtb_offset), (uint32_t)dtbsize);
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

    // Open the optional TAP interface and attach it to virtio-net.
    if (tappath != NULL) {
        // O_NONBLOCK at open time is authoritative; the fcntl below only
        // verifies it so a failed flag change cannot leave the main loop
        // blocking in read().
        tap_fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
        if (tap_fd < 0) {
            free(memory);
            printf("Error: cannot open /dev/net/tun.\n");
            return 2;
        }

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        strncpy(ifr.ifr_name, tappath, IFNAMSIZ - 1);
        if (ioctl(tap_fd, TUNSETIFF, &ifr) < 0) {
            close(tap_fd);
            tap_fd = -1;
            free(memory);
            printf("Error: cannot attach to TAP interface %s.\n", tappath);
            return 2;
        }

        int tap_flags = fcntl(tap_fd, F_GETFL, 0);
        if (tap_flags < 0 || (tap_flags & O_NONBLOCK) == 0) {
            close(tap_fd);
            tap_fd = -1;
            free(memory);
            printf("Error: cannot set the TAP interface nonblocking.\n");
            return 2;
        }

        tap_pollable = true;
        if (verbose) {
            printf("Attached TAP interface %s.\n", tappath);
        }
    }

    static const uint8_t net_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    virtio_blk_init(&virtio_blk, VIRTIO_BLK_ORIGIN, VIRTIO_BLK_SIZE, memory, RAM_ORIGIN, RAM_LENGTH, disk_capacity);
    virtio_console_init(&virtio_console, VIRTIO_CONSOLE_ORIGIN, VIRTIO_CONSOLE_SIZE, memory, RAM_ORIGIN, RAM_LENGTH);
    virtio_net_init(&virtio_net, VIRTIO_NET_ORIGIN, VIRTIO_NET_SIZE, memory, RAM_ORIGIN, RAM_LENGTH, net_mac);
    virtio_rng_init(&virtio_rng, VIRTIO_RNG_ORIGIN, VIRTIO_RNG_SIZE, memory, RAM_ORIGIN, RAM_LENGTH);
    goldfish_rtc_init(&goldfish_rtc, GOLDFISH_RTC_ORIGIN, GOLDFISH_RTC_SIZE);

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
    // -e <address> overrides the entry point.
    RiscvEmulatorState.programcounter = entry;
    RiscvEmulatorState.programcounternext = entry;

    if (restore_prefix != NULL && !snapshot_restore(restore_prefix)) {
        free(memory);
        return 2;
    }

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
    stdin_pollable = stdin_is_pollable();

// Polling stdin on every instruction slows the simulation drastically,
// so only do it once every STDIN_POLL_INTERVAL instructions.
#define STDIN_POLL_INTERVAL 4096

    for (;;) {
        if (snapshot_requested != 0) {
            snapshot_requested = 0;
            if (snapshot_prefix != NULL) {
                (void)snapshot_write(snapshot_prefix);
            }
        }

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
            board_idle();
        } else if (loopcounter % STDIN_POLL_INTERVAL == 0) {
            poll_stdin();
            poll_tap();
        }

        // If this prints then the instruction is not implemented.
        if (RiscvEmulatorState.instructionexecuted != 0 &&
            RiscvEmulatorState.instructionhandled == 0 &&
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

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    uint64_t user_ns = (uint64_t)usage.ru_utime.tv_sec * 1000000000u + (uint64_t)usage.ru_utime.tv_usec * 1000u;
    uint64_t sys_ns = (uint64_t)usage.ru_stime.tv_sec * 1000000000u + (uint64_t)usage.ru_stime.tv_usec * 1000u;
    uint64_t real_ns = monotonic_ns() - start_ns;
    double idle_percent = real_ns > 0 ? (double)idle_ns * 100.0 / (double)real_ns : 0.0;
    char idle_note[32];
    snprintf(idle_note, sizeof(idle_note), " (%.1f%% of real)", idle_percent);

    print_time("real", real_ns, "");
    print_time("user", user_ns, "");
    print_time("sys", sys_ns, "");
    print_time("idle", idle_ns, idle_note);
    printf("WFI: %llu calls, wakeups: %llu timer/cap, %llu stdin, %llu TAP, %llu signal, longest sleep %llu ms.\n",
           (unsigned long long)wfi_calls,
           (unsigned long long)wake_timer,
           (unsigned long long)wake_stdin,
           (unsigned long long)wake_tap,
           (unsigned long long)wake_signal,
           (unsigned long long)(longest_idle_ns / 1000000u));

    if (register_dump != 0 || stop_condition_hit != 0) {
        dump_registers(&RiscvEmulatorState);
    }
    if (memory_dump != 0) {
        dump_memory(memory_dump_address, memory_dump_length);
    }
    printf("Allocated %zu bytes of RAM (%zu bytes loaded from files).\n",
           (size_t)RAM_LENGTH, loaded_bytes);
    size_t touched_pages = ram_dirty_pages();
    uint64_t touched_bytes = (uint64_t)touched_pages * RAM_PAGE_SIZE;
    printf("RAM pages touched: %zu of %u (4 KiB pages) = %llu bytes (%llu KiB, %.2f MiB).\n",
           touched_pages, (unsigned)RAM_PAGE_COUNT,
           (unsigned long long)touched_bytes,
           (unsigned long long)(touched_bytes / 1024u),
           (double)touched_bytes / (1024.0 * 1024.0));
    free(memory);

    if (disk_fd >= 0) {
        close(disk_fd);
    }

    if (tap_fd >= 0) {
        close(tap_fd);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

    if (testresult == TEST_PASS) {
        return 0;
    }
    return 1;
}