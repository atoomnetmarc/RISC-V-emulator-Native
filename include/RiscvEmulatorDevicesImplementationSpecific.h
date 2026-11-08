/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
#else
    (void)state;
#endif
}

#endif
