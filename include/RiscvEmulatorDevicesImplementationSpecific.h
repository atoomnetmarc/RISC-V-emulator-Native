/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include <RiscvEmulatorDefine.h>

#include "memory.h"

#ifndef RiscvEmulatorDevicesImplementationSpecific_H_
#define RiscvEmulatorDevicesImplementationSpecific_H_

// SYSCON host hooks. The device library calls these through macros.
static inline void board_poweroff(uint8_t pass) {
    testresult = pass ? TEST_PASS : 1;
    pleasestop = 1;
}

static inline void board_reboot(void) {
    pleasestop = 1;
}

#define RVE_DEV_SYSCON_POWEROFF(pass) board_poweroff(pass)
#define RVE_DEV_SYSCON_REBOOT()       board_reboot()
#define RVE_DEV_UART16550_TX(byte)    putchar(byte)

// Writes console output to the host console.
static inline void board_console_write(const void *buffer, uint32_t length) {
    fwrite(buffer, 1, length, stdout);
}

#define RVE_DEV_VIRTIO_CONSOLE_TX(buffer, length) board_console_write(buffer, length)

// TAP file descriptor, opened by main() when -N <ifname> is given.
extern int tap_fd;

// Writes one Ethernet frame to the TAP interface.
static inline void board_net_write(const void *buffer, uint32_t length) {
    if (tap_fd >= 0) {
        (void)write(tap_fd, buffer, length);
    }
}

#define RVE_DEV_VIRTIO_NET_TX(buffer, length) board_net_write(buffer, length)

// Fills a buffer with host entropy.
static inline void board_rng_fill(void *buffer, uint32_t length) {
    uint8_t *bytes = buffer;

    while (length > 0) {
        ssize_t count = getrandom(bytes, length, 0);
        if (count > 0) {
            bytes += count;
            length -= (uint32_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        memset(bytes, 0, length);
        return;
    }
}

#define RVE_DEV_VIRTIO_RNG_FILL(buffer, length) board_rng_fill(buffer, length)

// Returns the host realtime clock in nanoseconds.
static inline uint64_t board_rtc_realtime_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_REALTIME, &value) != 0) {
        return 0;
    }

    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
}

// Returns the host monotonic clock in nanoseconds.
static inline uint64_t board_rtc_monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }

    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
}

#define RVE_DEV_GOLDFISH_RTC_REALTIME_NS()  board_rtc_realtime_ns()
#define RVE_DEV_GOLDFISH_RTC_MONOTONIC_NS() board_rtc_monotonic_ns()

// Disk file descriptor, opened by main() when -D <file> is given.
extern int disk_fd;

// Reads sectors from the host disk file.
static inline bool board_disk_read(uint64_t sector, void *buffer, uint32_t count) {
    if (disk_fd < 0) {
        return false;
    }

    size_t length = (size_t)count * 512u;
    return pread(disk_fd, buffer, length, (off_t)(sector * 512u)) == (ssize_t)length;
}

// Writes sectors to the host disk file.
static inline bool board_disk_write(uint64_t sector, const void *buffer, uint32_t count) {
    if (disk_fd < 0) {
        return false;
    }

    size_t length = (size_t)count * 512u;
    return pwrite(disk_fd, buffer, length, (off_t)(sector * 512u)) == (ssize_t)length;
}

// Flushes the host disk file.
static inline bool board_disk_flush(void) {
    if (disk_fd < 0) {
        return false;
    }

    return fsync(disk_fd) == 0;
}

#define RVE_DEV_VIRTIO_BLK_READ(sector, buffer, count)  board_disk_read(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_WRITE(sector, buffer, count) board_disk_write(sector, buffer, count)
#define RVE_DEV_VIRTIO_BLK_FLUSH()                      board_disk_flush()

#include <RiscvEmulatorDevices.h>

static syscon_t syscon = {.base = SYSCON_ORIGIN, .size = SYSCON_SIZE};

// CLINT memory range.
#define CLINT_ORIGIN 0x02000000
#define CLINT_SIZE   0x10000

static clint_t clint = {.base = CLINT_ORIGIN, .size = CLINT_SIZE, .mtimecmp = UINT64_MAX};

// UART memory range.
#define UART_ORIGIN 0x10000000
#define UART_SIZE   0x100

static uart16550_t uart = {.base = UART_ORIGIN, .size = UART_SIZE};

// PLIC memory range.
#define PLIC_ORIGIN 0x0C000000
#define PLIC_SIZE   0x400000

static plic_t plic = {.base = PLIC_ORIGIN, .size = PLIC_SIZE};

// Simple interrupt generator. The ACT platform macros write a set/clear word
// here to raise mip.MEIP. Bit 31 selects set; the low bits are the mip bits.
#define SIG_ORIGIN 0x0C400000

static uint32_t sig_pending;

/**
 * Writes the simple interrupt generator register.
 *
 * @param address The byte address in memory.
 * @param source The source address to copy the data from.
 * @param length The length in bytes of the data.
 * @return true when the address is the generator register, false otherwise.
 */
static inline bool sig_write(uint32_t address, const void *source, uint8_t length) {
    if (address != SIG_ORIGIN || length != sizeof(uint32_t)) {
        return false;
    }

    uint32_t value;
    memcpy(&value, source, sizeof(value));

    if ((value & 0x80000000u) != 0) {
        sig_pending |= value & 0x7FFFFFFFu;
    } else {
        sig_pending &= ~value;
    }

    return true;
}

// virtio-blk memory range.
#define VIRTIO_BLK_ORIGIN 0x10001000
#define VIRTIO_BLK_SIZE   0x1000

static virtio_blk_t virtio_blk;

// virtio-console memory range.
#define VIRTIO_CONSOLE_ORIGIN 0x10002000
#define VIRTIO_CONSOLE_SIZE   0x1000

static virtio_console_t virtio_console;

// virtio-net memory range.
#define VIRTIO_NET_ORIGIN 0x10003000
#define VIRTIO_NET_SIZE   0x1000

static virtio_net_t virtio_net;

// virtio-rng memory range.
#define VIRTIO_RNG_ORIGIN 0x10004000
#define VIRTIO_RNG_SIZE   0x1000

static virtio_rng_t virtio_rng;

// Goldfish RTC memory range.
#define GOLDFISH_RTC_ORIGIN 0x10005000
#define GOLDFISH_RTC_SIZE   0x1000

static goldfish_rtc_t goldfish_rtc;

/**
 * Loads bytes from the devices to the RISC-V hart.
 *
 * @param address The byte address in memory.
 * @param destination The destination address to copy the data to.
 * @param length The length in bytes of the data.
 * @return true when the address is mapped, false otherwise.
 */
static inline bool RiscvDevicesLoad(uint32_t address, void *destination, uint8_t length) {
    if (clint_read(&clint, address, destination, length)) {
        return true;
    }

    if (uart16550_read(&uart, address, destination, length)) {
        return true;
    }

    if (plic_read(&plic, address, destination, length)) {
        return true;
    }

    if (virtio_blk_read(&virtio_blk, address, destination, length)) {
        return true;
    }

    if (virtio_console_read(&virtio_console, address, destination, length)) {
        return true;
    }

    if (virtio_net_read(&virtio_net, address, destination, length)) {
        return true;
    }

    if (virtio_rng_read(&virtio_rng, address, destination, length)) {
        return true;
    }

    if (goldfish_rtc_read(&goldfish_rtc, address, destination, length)) {
        return true;
    }

    if (syscon_read(&syscon, address, destination, length)) {
        return true;
    }

    return false;
}

/**
 * Stores bytes from the RISC-V hart to the devices.
 *
 * @param address The byte address in memory.
 * @param source The source address to copy the data from.
 * @param length The length in bytes of the data.
 * @return true when the address is mapped, false otherwise.
 */
static inline bool RiscvDevicesStore(uint32_t address, const void *source, uint8_t length) {
    if (sig_write(address, source, length)) {
        return true;
    }

    if (clint_write(&clint, address, source, length)) {
        return true;
    }

    if (uart16550_write(&uart, address, source, length)) {
        return true;
    }

    if (plic_write(&plic, address, source, length)) {
        return true;
    }

    if (virtio_blk_write(&virtio_blk, address, source, length)) {
        return true;
    }

    if (virtio_console_write(&virtio_console, address, source, length)) {
        return true;
    }

    if (virtio_net_write(&virtio_net, address, source, length)) {
        return true;
    }

    if (virtio_rng_write(&virtio_rng, address, source, length)) {
        return true;
    }

    if (goldfish_rtc_write(&goldfish_rtc, address, source, length)) {
        return true;
    }

    if (syscon_write(&syscon, address, source, length)) {
        return true;
    }

    return false;
}

/**
 * Advances the CLINT from the host clock and bridges the pending bits into mip.
 */
static inline void RiscvDevicesTick(RiscvEmulatorState_t *state) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_us = (uint64_t)ts.tv_sec * 1000000u + (uint32_t)ts.tv_nsec / 1000u;
    clint_tick(&clint, now_us);

    plic_set_pending(&plic, 1, virtio_blk_irq_pending(&virtio_blk));
    plic_set_pending(&plic, 2, virtio_console_irq_pending(&virtio_console));
    plic_set_pending(&plic, 3, virtio_net_irq_pending(&virtio_net));
    plic_set_pending(&plic, 4, uart16550_irq_pending(&uart));
    plic_set_pending(&plic, 5, virtio_rng_irq_pending(&virtio_rng));

#if (RVE_E_ZICSR == 1)
    if (clint_timer_pending(&clint)) {
        state->csr.mip_platform |= (1u << 7);
    } else {
        state->csr.mip_platform &= ~(1u << 7);
    }

    if (clint_software_pending(&clint)) {
        state->csr.mip_platform |= (1u << 3);
    } else {
        state->csr.mip_platform &= ~(1u << 3);
    }

    if (plic_irq_pending(&plic, 0)) {
        state->csr.mip_platform |= (1u << 11);
    } else {
        state->csr.mip_platform &= ~(1u << 11);
    }

#if (RVE_E_SV32 == 1)
    if (plic_irq_pending(&plic, 1)) {
        state->csr.mip_platform |= (1u << 9);
    } else {
        state->csr.mip_platform &= ~(1u << 9);
    }
#endif

    // The simple interrupt generator adds the platform bits that software
    // raises through the ACT macros.
    state->csr.mip_platform |= sig_pending;
#else
    (void)state;
#endif
}

#endif
