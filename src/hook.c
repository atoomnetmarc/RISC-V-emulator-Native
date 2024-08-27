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

// Extension: I

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

    printf("pc: 0x%08x, instruction: 0x%08x, %s rd x%u(%s): 0x%08x, rs1 x%u(%s): 0x%08x, rs2 x%u(%s): 0x%08x\n",
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

    printf("                                         x%u(%s) = 0x%08x\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
}

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
        printf("pc: 0x%08x, instruction: 0x%08x, nop\n",
               state->programcounter,
               state->instruction.value);
        return;
    }

    // Detect pseudoinstruction MV
    if (strcmp(instruction, "addi") == 0 &&
        imm == 0) {
        printf("pc: 0x%08x, instruction: 0x%08x, mv rd x%u(%s): 0x%08x, rs1 x%u(%s): 0x%08x\n",
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

    printf("pc: 0x%08x, instruction: 0x%08x, %s rd x%u(%s): 0x%08x, rs1 x%u(%s): 0x%08x, imm 0x%08x\n",
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

    printf("                                         x%u(%s) = 0x%08x\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
}

void RiscvEmulatorAUIPCHookBegin(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm31_12,
    const uint32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("pc: 0x%08x, instruction: 0x%08x, auipc rd x%u(%s): 0x%08x, imm31_12: 0x%05x (imm: 0x%08x)\n",
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

    printf("                                         x%u(%s) = 0x%08x\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
}

// Extension: Zicsr

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

    printf("pc: 0x%08x, instruction: 0x%08x, %s rd x%u(%s): 0x%08x, rs1 x%u(%s): 0x%08x, csr 0x%04x(%s): 0x%08x\n",
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

    printf("                                         x%u(%s) = 0x%08x\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);

    printf("                                         %s = 0x%08x\n",
           csrname,
           *(uint32_t *)csr);
}

#pragma GCC diagnostic pop