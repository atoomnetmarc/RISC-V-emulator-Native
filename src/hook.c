/*

Copyright 2023-2024 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <RiscvEmulatorDebug.h>
#include <RiscvEmulatorTypeEmulator.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#if (RVE_E_ZICSR == 1)
/**
 * Debug prints for trap.
 */
void RiscvEmulatorTrapHookBegin(
    const RiscvEmulatorState_t *state) {

    const char *causedescription = RiscvEmulatorGetMcauseException(
        state->csr.mcause.interrupt,
        state->csr.mcause.exceptioncode);

    printf("                                         trap, interrupt: %d, exception code %d: %s\n",
           state->csr.mcause.interrupt,
           state->csr.mcause.exceptioncode,
           causedescription);
    printf("                                         mstatus.mpie = %d\n",
           state->csr.mstatus.mpie);
    printf("                                         mstatus.mie = %d\n",
           state->csr.mstatus.mie);
    printf("                                         mepc = 0x%08X\n",
           state->csr.mepc);
    printf("                                         pc = 0x%08X\n",
           state->programcounternext);
}
#endif

/**
 * Debug prints for Integer Register-Register Operations.
 */
void RiscvEmulatorIntRegRegHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const uint8_t rs2num,
    const void *rs2) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);
    const char *rs2name = RiscvEmulatorGetRegisterSymbolicName(rs2num);

    printf("pc: 0x%08X, instruction: 0x%08X, %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           rs2num,
           rs2name,
           *(uint32_t *)rs2);
}

void RiscvEmulatorIntRegRegHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const uint8_t rs2num,
    const void *rs2) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("                                         x%u(%s) = 0x%08X\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
}

/**
 * Debug prints for Integer Register-Immediate Instructions.
 */
void RiscvEmulatorIntRegImmHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);

    // Detect pseudoinstruction NOP
    if (strcmp(instruction, "addi") == 0 &&
        rdnum == 0 &&
        rs1num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, nop\n",
               state->programcounter,
               state->instruction.value);
        return;
    }

    // Detect pseudoinstruction MV
    if (strcmp(instruction, "addi") == 0 &&
        imm == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, mv, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X\n",
               state->programcounter,
               state->instruction.value,
               rdnum,
               rdname,
               *(uint32_t *)rd,
               rs1num,
               rs1name,
               *(uint32_t *)rs1);
        return;
    }

    printf("pc: 0x%08X, instruction: 0x%08X, %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, imm: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           imm);
}

void RiscvEmulatorIntRegImmHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("                                         x%u(%s) = 0x%08X\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
}

/**
 * Debug print for Load Operations.
 */
void RiscvEmulatorLoadHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const int16_t imm,
    const uint32_t memorylocation,
    const uint8_t length) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);

    printf("pc: 0x%08X, instruction: 0x%08X, %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, imm: 0x%04X(%d), memorylocation: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           (uint16_t)imm,
           imm,
           memorylocation);
}

void RiscvEmulatorLoadHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const int16_t imm,
    const uint32_t memorylocation,
    const uint8_t length) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    if (strcmp(instruction, "lb") == 0) {
        printf("                                         x%u(%s) = %i\n",
               rdnum,
               rdname,
               *(int8_t *)rd);
        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    } else if (strcmp(instruction, "lbu") == 0) {
        printf("                                         x%u(%s) = %u\n",
               rdnum,
               rdname,
               *(uint8_t *)rd);
        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    } else if (strcmp(instruction, "lh")) {
        printf("                                         x%u(%s) = %i\n",
               rdnum,
               rdname,
               *(int16_t *)rd);
        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    } else if (strcmp(instruction, "lhu")) {
        printf("                                         x%u(%s) = %u\n",
               rdnum,
               rdname,
               *(uint16_t *)rd);
        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    } else {
        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    }
}

/**
 * Debug print for Store Operations.
 */
void RiscvEmulatorStoreHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rs1num,
    const void *rs1,
    const uint8_t rs2num,
    const void *rs2,
    const int16_t imm,
    const uint32_t memorylocation,
    const uint8_t length) {

    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);
    const char *rs2name = RiscvEmulatorGetRegisterSymbolicName(rs2num);

    printf("pc: 0x%08X, instruction: 0x%08X, %s, rs1 x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X, imm: 0x%04X(%d), memorylocation: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           rs2num,
           rs2name,
           *(uint32_t *)rs2,
           (uint16_t)imm,
           imm,
           memorylocation);
}

/**
 * Generic hook function for Store Operations.
 */
void RiscvEmulatorStoreHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rs1num,
    const void *rs1,
    const uint8_t rs2num,
    const void *rs2,
    const int16_t imm,
    const uint32_t memorylocation,
    const uint8_t length) {

    if (strcmp(instruction, "sb") == 0) {
        printf("                                         0x%08X = 0x%02X\n",
               memorylocation,
               *(uint8_t *)rs2);
    } else if (strcmp(instruction, "sh")) {
        printf("                                         0x%08X = 0x%04X\n",
               memorylocation,
               *(uint16_t *)rs2);
    } else {
        printf("                                         0x%08X = 0x%08X\n",
               memorylocation,

               *(uint32_t *)rs2);
    }
}

/**
 * Debug print for Branch Operations.
 */
void RiscvEmulatorBranchHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rs1num,
    const void *rs1,
    const uint8_t rs2num,
    const void *rs2,
    const int16_t imm) {

    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);
    const char *rs2name = RiscvEmulatorGetRegisterSymbolicName(rs2num);

    printf("pc: 0x%08X, instruction: 0x%08X, %s, rs1 x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           rs2num,
           rs2name,
           *(uint32_t *)rs2,
           (uint16_t)imm,
           imm);
}

void RiscvEmulatorBranchHookEnd(
    const RiscvEmulatorState_t *state) {

    printf("                                         pc = 0x%08X\n",
           state->programcounternext);
}

/**
 * Debug print for: AUIPC
 */
void RiscvEmulatorAUIPCHookBegin(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm31_12,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("pc: 0x%08X, instruction: 0x%08X, auipc, rd x%u(%s): 0x%08X, imm31_12: 0x%05x (imm: 0x%08X)\n",
           state->programcounter,
           state->instruction.value,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           imm31_12,
           imm);
}

void RiscvEmulatorAUIPCHookEnd(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm31_12,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("                                         x%u(%s) = 0x%08X\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
}

/**
 * Debug print for: LUI
 */
void RiscvEmulatorLUIHookBegin(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("pc: 0x%08X, instruction: 0x%08X, lui, rd x%u(%s): 0x%08X, imm: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           imm);
}

void RiscvEmulatorLUIHookEnd(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm) {

    if (rdnum != 0) {
        const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    }
}

/**
 * Debug print for: JAL
 */
void RiscvEmulatorJALHookBegin(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    // Detect pseudoinstruction J
    if (rdnum == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, j, imm: 0x%08X\n",
               state->programcounter,
               state->instruction.value,
               imm);
        return;
    }

    printf("pc: 0x%08X, instruction: 0x%08X, jal, rd x%u(%s): 0x%08X, imm: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           imm);
}

void RiscvEmulatorJALHookEnd(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm) {

    if (rdnum != 0) {
        const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    }

    printf("                                         pc = 0x%08X\n",
           state->programcounternext);
}

/**
 * Debug print for: JALR
 */
void RiscvEmulatorJALRHookBegin(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const int16_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);

    // Detect pseudoinstruction RET
    if (rdnum == 0 && rs1num == 1 && imm == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, ret, rs1 x%u(%s): 0x%08X\n",
               state->programcounter,
               state->instruction.value,
               rs1num,
               rs1name,
               *(uint32_t *)rs1);
        return;
    }

    // Detect pseudoinstruction JR
    if (rdnum == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, jr, rs1 x%u(%s): 0x%08X imm: 0x%04X\n",
               state->programcounter,
               state->instruction.value,
               rs1num,
               rs1name,
               *(uint32_t *)rs1,
               (uint16_t)imm);
        return;
    }

    printf("pc: 0x%08X, instruction: 0x%08X, jalr, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, imm: 0x%04X\n",
           state->programcounter,
           state->instruction.value,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           (uint16_t)imm);
}

void RiscvEmulatorJALRHookEnd(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const int16_t imm) {

    if (rdnum != 0) {
        const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

        printf("                                         x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    }

    printf("                                         pc = 0x%08X\n",
           state->programcounternext);
}

#if (RVE_E_ZICSR == 1)

/**
 * Debug prints for CSRR* instructions.
 */
void RiscvEmulatorCSRR_HookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const uint16_t csrnum,
    const void *csr) {

    const char *csrname = RiscvEmulatorGetCSRName(csrnum);
    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);

    printf("pc: 0x%08X, instruction: 0x%08X, %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, csr 0x%04X(%s): 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           rs1num,
           rs1name,
           *(uint32_t *)rs1,
           csrnum,
           csrname,
           *(uint32_t *)csr);
}

void RiscvEmulatorCSRR_HookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs1num,
    const void *rs1,
    const uint16_t csrnum,
    const void *csr) {

    const char *csrname = RiscvEmulatorGetCSRName(csrnum);
    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("                                         x%u(%s) = 0x%08X\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);

    printf("                                         %s = 0x%08X\n",
           csrname,
           *(uint32_t *)csr);
}

#endif

#pragma GCC diagnostic pop