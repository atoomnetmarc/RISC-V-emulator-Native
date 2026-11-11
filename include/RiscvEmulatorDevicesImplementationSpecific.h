/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

// virtio-blk memory range.
#define VIRTIO_BLK_ORIGIN 0x10001000
#define VIRTIO_BLK_SIZE   0x1000

static virtio_blk_t virtio_blk = {.base = VIRTIO_BLK_ORIGIN, .size = VIRTIO_BLK_SIZE};

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

#if (RVE_E_ZICSR == 1)
    if (clint_timer_pending(&clint)) {
        state->csr.mip.mip |= (1u << 7);
    } else {
        state->csr.mip.mip &= ~(1u << 7);
    }

    if (clint_software_pending(&clint)) {
        state->csr.mip.mip |= (1u << 3);
    } else {
        state->csr.mip.mip &= ~(1u << 3);
    }

    if (plic_irq_pending(&plic)) {
        state->csr.mip.mip |= (1u << 11);
    } else {
        state->csr.mip.mip &= ~(1u << 11);
    }
#else
    (void)state;
#endif
}

#endif
