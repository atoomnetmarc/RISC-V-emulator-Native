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
#include <RiscvEmulatorType.h>

#include "RiscvEmulatorDevicesImplementationSpecific.h"
#include "memory.h"

#ifndef RiscvEmulatorImplementationSpecific_H_
#define RiscvEmulatorImplementationSpecific_H_

/**
 * Real-time counter for the `time` CSR.
 *
 * The RISC-V `time` CSR is a free-running real-time counter, not an
 * instruction counter (zicntr.adoc:116). On the Native platform the host
 * clock is the natural source. The unit is nanoseconds; the guest only
 * requires a monotonic-enough counter, not a specific frequency.
 */
static inline uint64_t RiscvEmulatorTime(void) {
    struct timespec ts = {0};
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define RVE_TIME_SOURCE(state) RiscvEmulatorTime()

/**
 * Loads bytes from emulator to RISC-V.
 *
 * @param address The byte address in memory.
 * @param destination The destination address to copy the data to.
 * @param length The length in bytes of the data.
 * @return true when the address is mapped, false otherwise.
 */
static inline bool RiscvEmulatorLoad(uint32_t address, void *destination, uint8_t length) {
    if (RiscvDevicesLoad(address, destination, length)) {
        return true;
    }

    if (address < RAM_ORIGIN || address >= RAM_ORIGIN + RAM_LENGTH) {
        return false;
    }

    memcpy(destination, &memory[address - RAM_ORIGIN], length);
    return true;
}

/**
 * Stores bytes from RISC-V to emulator.
 *
 * @param address The byte address in memory.
 * @param source The source address to copy the data from.
 * @param length The length in bytes of the data.
 * @return true when the address is mapped, false otherwise.
 */
static inline bool RiscvEmulatorStore(uint32_t address, const void *source, uint8_t length) {
    // HALT test termination. RVMODEL_HALT_PASS writes TEST_PASS (123456789) to
    // 0x20000000; RVMODEL_HALT_FAIL writes 1. Both end in an infinite self-loop,
    // so stop here.
    if (address == 0x20000000) {
        memcpy(&testresult, source, 4);
        pleasestop = 1;
        return true;
    }

    if (RiscvDevicesStore(address, source, length)) {
        return true;
    }

    if (address < RAM_ORIGIN || address >= RAM_ORIGIN + RAM_LENGTH) {
        return false;
    }

    memcpy(&memory[address - RAM_ORIGIN], source, length);
    return true;
}

/**
 * Handles a fault where the instruction is somehow not recognized.
 *
 * Something wrong happened at the current programcounter.
 * Use a listing file of the risc-v program to better understand the wrong.
 * The failed machine instruction is found in state.instruction.value.
 */
static inline void RiscvEmulatorIllegalInstruction(RiscvEmulatorState_t *state __attribute__((unused))) {

    // When there is no trap handler, stop the emulation.
#if (RVE_E_ZICSR == 1)
    if (state->csr.mtvec.base == 0)
#endif
    {
        printf("There is no trap handler. Stop emulating.\n");
        pleasestop = 1;
    }
}

/**
 * Handles an ECALL.
 *
 * The exit() semihosting convention is only applied when there is no trap
 * handler. A real kernel installs mtvec and expects the ECALL to trap.
 */
static inline void RiscvEmulatorHandleECALL(RiscvEmulatorState_t *state) {
#if (RVE_E_ZICSR == 1)
    if (state->csr.mtvec.base != 0) {
        return;
    }
#endif

    if (state->reg.a7 == 93) {
        printf("The ecall requested is exit(%u). This means we are done emulating.\n",
               state->reg.a0);
        pleasestop = 1;
    }
}

/**
 * Handles an EBREAK.
 */
static inline void RiscvEmulatorHandleEBREAK(RiscvEmulatorState_t *state __attribute__((unused))) {
}

#endif
