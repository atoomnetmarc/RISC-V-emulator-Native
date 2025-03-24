/*

Copyright 2023-2025 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <string.h>

#include <RiscvEmulatorDefine.h>
#include <RiscvEmulatorType.h>

#include "memory.h"

#ifndef RiscvEmulatorImplementationSpecific_H_
#define RiscvEmulatorImplementationSpecific_H_

/**
 * Loads bytes from emulator to RISC-V.
 *
 * @param address The byte address in memory.
 * @param destination The destination address to copy the data to.
 * @param length The length in bytes of the data.
 */
static inline void RiscvEmulatorLoad(uint32_t address, void *destination, uint8_t length) {
    if (address >= RAM_ORIGIN + RAM_LENGTH) {
        printf("Loading from address after RAM will not work. Stopping emulation.\n");
        pleasestop = 1;
    } else if (address >= RAM_ORIGIN) {
        memcpy(destination, &memory[address - RAM_ORIGIN], length);
    } else if (address >= ROM_ORIGIN) {
        printf("RiscvEmulatorLoad from ROM.\n");
        uint32_t addressinfirmware = address - ROM_ORIGIN;
        if (addressinfirmware + length >= sizeof(firmware)) {
            printf("Loading instructions from address after ROM will not work. Stopping emulation.\n");
            pleasestop = 1;
            return;
        }
        memcpy(destination, &firmware[addressinfirmware], length);
    } else if (address >= IO_ORIGIN) {
        printf("Loading from IO does not work.\n");
    }
}

/**
 * Stores bytes from RISC-V to emulator.
 *
 * @param address The byte address in memory.
 * @param source The source address to copy the data from.
 * @param length The length in bytes of the data.
 */
static inline void RiscvEmulatorStore(uint32_t address, const void *source, uint8_t length) {
    if (address >= RAM_ORIGIN + RAM_LENGTH) {
        printf("Writing to address after RAM will not work. Stopping emulation.\n");
        pleasestop = 1;
    } else if (address >= RAM_ORIGIN) {
        memcpy(&memory[address - RAM_ORIGIN], source, length);
    } else if (address >= ROM_ORIGIN) {
        printf("RiscvEmulatorStore to ROM.\n");
        memcpy(&firmware[address - ROM_ORIGIN], source, length);
    } else if (address >= IO_ORIGIN) {
        printf("Writing to IO does not work.\n");
    }
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

#if (RVE_E_ZICSR == 1)
/**
 * Handles a fault where the CSR is not recognized.
 */
static inline void RiscvEmulatorUnknownCSR(RiscvEmulatorState_t *state) {

    printf("Unknown or not implemented CSR. pc: 0x%08X, instruction: 0x%08X, csr: 0x%04X\n",
           state->programcounter,
           state->instruction.value,
           state->instruction.itypecsr.csr);

    // Requesting stop.
    pleasestop = 1;
}
#endif

/**
 * Handles an ECALL.
 */
static inline void RiscvEmulatorHandleECALL(RiscvEmulatorState_t *state) {
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
