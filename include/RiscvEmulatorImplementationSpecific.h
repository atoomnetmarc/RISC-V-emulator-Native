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
    if (address >= RAM_ORIGIN) {
        memcpy(destination, &memory[address - RAM_ORIGIN], length);
    } else if (address >= ROM_ORIGIN) {
        // This is ROM addressing.
    } else if (address >= IO_ORIGIN) {
        // This emulator has no IO.
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
    if (address >= RAM_ORIGIN) {
        memcpy(&memory[address - RAM_ORIGIN], source, length);
    } else if (address >= ROM_ORIGIN) {
        // This is ROM addressing. ROM cannot be written.
    } else if (address >= IO_ORIGIN) {
        // This emulator has no IO.
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
}

/**
 * Handles a fault where the CSR is not recognized.
 */
inline void RiscvEmulatorUnknownCSR(RiscvEmulatorState_t *state) {
}

/**
 * Handles an ECALL.
 */
inline void RiscvEmulatorHandleECALL(RiscvEmulatorState_t *state) {
}

#endif
