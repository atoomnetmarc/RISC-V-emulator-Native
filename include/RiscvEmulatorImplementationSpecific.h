/*

Copyright 2023-2024 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <string.h>

#include <RiscvEmulatorDefine.h>
#include <RiscvEmulatorType.h>

#include "memory.h"

#ifndef RiscvEmulatorImplementationSpecific_H_
#define RiscvEmulatorImplementationSpecific_H_

/**
 * Loads a 32-bit RISC-V instruction from the specified address.
 *
 * @param address The address in bytes of where to read the RISC-V instruction.
 * @return 32-bit RISC-V instruction.
 */
inline uint32_t RiscvEmulatorLoadInstruction(uint32_t address) {
    uint32_t instruction;
    memcpy(&instruction, &firmware[address - ROM_ORIGIN], sizeof(instruction));
    return instruction;
}

/**
 * Loads bytes from emulator to RISC-V.
 *
 * @param address The byte address in memory.
 * @param destination The destination address to copy the data to.
 * @param length The length in bytes of the data.
 */
inline void RiscvEmulatorLoad(uint32_t address, void *destination, uint8_t length) {
    printf("RiscvEmulatorLoad address 0x%08x\n", address);

    if (address >= RAM_ORIGIN + RAM_LENGTH) {
        printf("Loading from address after RAM will not work. Stopping emulation.\n");
        pleasestop = 1;
    } else if (address >= RAM_ORIGIN) {
        memcpy(destination, &memory[address - RAM_ORIGIN], length);
    } else if (address >= ROM_ORIGIN) {
        printf("RiscvEmulatorLoad from ROM.\n");
        memcpy(destination, &firmware[address - ROM_ORIGIN], length);
    } else if (address >= IO_ORIGIN) {
        printf("Loading from IO does not work.\n");
    }
}

/**
 * Stores bytes from RISC-V to emulator.
 *
 * @param address The byte address in memory.
 * @param destination The destination address to copy the data to.
 * @param length The length in bytes of the data.
 */
inline void RiscvEmulatorStore(uint32_t address, const void *source, uint8_t length) {
    printf("RiscvEmulatorStore address 0x%08x\n", address);

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
inline void RiscvEmulatorUnknownInstruction(RiscvEmulatorState_t *state) {
    printf("Unknown or not implemented RISC-V instruction. pc: 0x%08x, instruction: 0x%08x\n",
           state->programcounter,
           state->instruction.value);

    // Requesting stop.
    pleasestop = 1;
}

#if (RVE_E_ZICSR == 1)
/**
 * Handles a fault where the CSR is not recognized.
 */
inline void RiscvEmulatorUnknownCSR(RiscvEmulatorState_t *state) {

    printf("Unknown or not implemented CSR. pc: 0x%08x, instruction: 0x%08x, csr: 0x%04x\n",
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
inline void RiscvEmulatorHandleECALL(RiscvEmulatorState_t *state) {
    printf("Simulated RISC-V executed ecall! (a0: %u, a7: %u)\n",
           state->registers.symbolic.a0,
           state->registers.symbolic.a7);

    if (state->registers.symbolic.a7 == 93) {
        printf("The ecall requested is exit(%u). This means we are done emulating.\n",
               state->registers.symbolic.a0);
        pleasestop = 1;
    }
}

/**
 * Handles an EBREAK.
 */
inline void RiscvEmulatorHandleEBREAK(RiscvEmulatorState_t *state) {
    printf("Simulated RISC-V executed ebreak! pc: 0x%08x\n",
           state->programcounter);
}

#endif
