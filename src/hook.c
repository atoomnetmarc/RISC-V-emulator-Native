/*

Copyright 2023-2024 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <RiscvEmulatorDebug.h>
#include <RiscvEmulatorDefineHook.h>
#include <RiscvEmulatorDefineOpcode.h>
#include <RiscvEmulatorTypeEmulator.h>
#include <RiscvEmulatorTypeHook.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

void printInteger(
    const char *name,
    const uint32_t value,
    const uint8_t length,
    const uint8_t issigned) {

    printf(", %s: ", name);

    switch (length) {
        case 1: {
            printf("0x%02X", (uint8_t)value);
            break;
        }
        case 2: {
            printf("0x%04X", (uint16_t)value);
            break;
        }
        default: {
            printf("0x%08X", value);
            break;
        }
    }

    if (issigned) {
        printf("(%d)", (int32_t)value);
    } else {
        printf("(%d)", value);
    }
}

/**
 * Debug prints.
 */
void RiscvEmulatorHook(
    const RiscvEmulatorState_t *state,
    const RiscvEmulatorHookContext_t *context) {

    const void *rd = context->rd;
    const uint8_t rdnum = context->rdnum;
    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const void *rs1 = context->rs1;
    const uint8_t rs1num = context->rs1num;
    const char *rs1name = RiscvEmulatorGetRegisterSymbolicName(rs1num);
    const void *rs2 = context->rs2;
    const uint8_t rs2num = context->rs2num;
    const char *rs2name = RiscvEmulatorGetRegisterSymbolicName(rs2num);

#if (RVE_E_ZICSR == 1)
    const uint8_t csrnum = context->csrnum;
    const char *csrname = RiscvEmulatorGetCSRName(csrnum);
#endif

    const uint32_t imm = context->imm;
    const uint8_t immissigned = context->immissigned;
    char *immname = "imm";
    if (context->immname != NULL) {
        immname = context->immname;
    }
    uint8_t immlength = 4;
    if (context->immlength != 0) {
        immlength = context->immlength;
    }

    const uint32_t memorylocation = context->memorylocation;
    const uint8_t length = context->length;

    const void *sp = &state->registers.symbolic.sp;

    const char *tab = "                                         ";

    if (context->hook == HOOK_UNKNOWN ||
        context->hook == HOOK_BEGIN) {
        printf("pc: 0x%08X", state->programcounter);

#if (RVE_E_C == 1)
        if (state->instruction.copcode.op != OPCODE16_QUADRANT_INVALID) {
            printf(", instruction:     0x%04X", (uint16_t)state->instruction.value);
        } else
#endif
        {
            printf(", instruction: 0x%08X", state->instruction.value);
        }

        if (context->instruction == NULL ||
            context->instruction[0] == '\0') {
            printf(", ??? instruction string not set");
            return;
        }
    }

    // I

    // Detect pseudoinstruction neg
    if (strcmp(context->instruction, "sub") == 0 &&
        rs1num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", neg, rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            return;
        }
    }

    // Detect pseudoinstruction snez
    if (strcmp(context->instruction, "sltu") == 0 &&
        rs1num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", snez, rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            return;
        }
    }

    // Detect pseudoinstruction sltz
    if (strcmp(context->instruction, "slt") == 0 &&
        rs2num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", sltz, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        }
    }

    // Detect pseudoinstruction sgtz
    if (strcmp(context->instruction, "slt") == 0 &&
        rs1num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", sgtz, rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            return;
        }
    }

    if (strcmp(context->instruction, "add") == 0 ||
        strcmp(context->instruction, "sub") == 0 ||
        strcmp(context->instruction, "sll") == 0 ||
        strcmp(context->instruction, "slt") == 0 ||
        strcmp(context->instruction, "sltu") == 0 ||
        strcmp(context->instruction, "xor") == 0 ||
        strcmp(context->instruction, "srl") == 0 ||
        strcmp(context->instruction, "sra") == 0 ||
        strcmp(context->instruction, "or") == 0 ||
        strcmp(context->instruction, "and") == 0 ||
        strcmp(context->instruction, "shadd") == 0 ||
        strcmp(context->instruction, "andn") == 0 ||
        strcmp(context->instruction, "orn") == 0 ||
        strcmp(context->instruction, "xnor") == 0 ||
        strcmp(context->instruction, "max") == 0 ||
        strcmp(context->instruction, "maxu") == 0 ||
        strcmp(context->instruction, "min") == 0 ||
        strcmp(context->instruction, "minu") == 0 ||
        strcmp(context->instruction, "rol") == 0 ||
        strcmp(context->instruction, "ror") == 0 ||
        strcmp(context->instruction, "clmul") == 0 ||
        strcmp(context->instruction, "clmulh") == 0 ||
        strcmp(context->instruction, "clmulr") == 0 ||
        strcmp(context->instruction, "bclr") == 0 ||
        strcmp(context->instruction, "bext") == 0 ||
        strcmp(context->instruction, "binv") == 0 ||
        strcmp(context->instruction, "bset") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            return;
        } else if (context->hook == HOOK_END) {
            if (rdnum != 0) {
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            }
            return;
        }
    }

    // Detect pseudoinstruction nop
    if (strcmp(context->instruction, "addi") == 0 &&
        rdnum == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", nop\n");
            return;
        } else if (context->hook == HOOK_END) {
            return;
        }
    }

    // Detect pseudoinstruction mv
    if (strcmp(context->instruction, "addi") == 0 &&
        imm == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", mv, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        }
    }

    // Detect pseudoinstruction not
    if (strcmp(context->instruction, "xori") == 0 &&
        (int16_t)imm == -1) {
        if (context->hook == HOOK_BEGIN) {
            printf(", not, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        }
    }

    // Detect pseudoinstruction seqz
    if (strcmp(context->instruction, "sltiu") == 0 &&
        imm == 1) {
        if (context->hook == HOOK_BEGIN) {
            printf(", seqz, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X\n",
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        }
    }

    if (strcmp(context->instruction, "addi") == 0 ||
        strcmp(context->instruction, "slli") == 0 ||
        strcmp(context->instruction, "slti") == 0 ||
        strcmp(context->instruction, "sltiu") == 0 ||
        strcmp(context->instruction, "xori") == 0 ||
        strcmp(context->instruction, "srli") == 0 ||
        strcmp(context->instruction, "srai") == 0 ||
        strcmp(context->instruction, "ori") == 0 ||
        strcmp(context->instruction, "andi") == 0 ||
        strcmp(context->instruction, "rori") == 0 ||
        strcmp(context->instruction, "bclri") == 0 ||
        strcmp(context->instruction, "bexti") == 0 ||
        strcmp(context->instruction, "binvi") == 0 ||
        strcmp(context->instruction, "bseti") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            if (rdnum != 0) {
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            }
            return;
        }
    }

    if (strcmp(context->instruction, "lb") == 0 ||
        strcmp(context->instruction, "lbu") == 0 ||
        strcmp(context->instruction, "lh") == 0 ||
        strcmp(context->instruction, "lhu") == 0 ||
        strcmp(context->instruction, "lw") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf(", memorylocation: 0x%08X\n",
                   memorylocation);
            return;
        } else if (context->hook == HOOK_END) {
            if (strcmp(context->instruction, "lb") == 0) {
                printf("%sx%u(%s) = %i\n",
                       tab,
                       rdnum,
                       rdname,
                       *(int8_t *)rd);
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            } else if (strcmp(context->instruction, "lbu") == 0) {
                printf("%sx%u(%s) = %u\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint8_t *)rd);
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            } else if (strcmp(context->instruction, "lh") == 0) {
                printf("%sx%u(%s) = %i\n",
                       tab,
                       rdnum,
                       rdname,
                       *(int16_t *)rd);
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            } else if (strcmp(context->instruction, "lhu") == 0) {
                printf("%sx%u(%s) = %u\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint16_t *)rd);
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            } else {
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            }
            return;
        }
    }

    if (strcmp(context->instruction, "sb") == 0 ||
        strcmp(context->instruction, "sh") == 0 ||
        strcmp(context->instruction, "sw") == 0 ||
        strcmp(context->instruction, "c.sw") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rs1 x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X",
                   context->instruction,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            printInteger(immname, imm, immlength, immissigned);
            printf(", memorylocation: 0x%08X\n",
                   memorylocation);
            return;
        } else if (context->hook == HOOK_END) {
            if (length == 1) {
                printf("%s0x%08X = 0x%02X\n",
                       tab,
                       memorylocation,
                       *(uint8_t *)rs2);
            } else if (length == 2) {
                printf("%s0x%08X = 0x%04X\n",
                       tab,
                       memorylocation,
                       *(uint16_t *)rs2);
            } else {
                printf("%s0x%08X = 0x%08X\n",
                       tab,
                       memorylocation,
                       *(uint32_t *)rs2);
            }
            return;
        }
    }

    // M

    // A

    // F

    // D

    // Q

    // C

    // Detect pseudoinstruction c.nop
    if (strcmp(context->instruction, "c.addi") == 0 &&
        rdnum == 0 &&
        rs1num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", c.nop\n");
            return;
        } else if (context->hook == HOOK_END) {
            return;
        }
    }

    if (strcmp(context->instruction, "c.addi4spn") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, sp: 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   *(uint32_t *)sp);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            if (rdnum != 0) {
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            }
            return;
        }
    }

    if (strcmp(context->instruction, "c.li") == 0 ||
        strcmp(context->instruction, "c.lui") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            if (rdnum != 0) {
                printf("%sx%u(%s) = 0x%08X\n",
                       tab,
                       rdnum,
                       rdname,
                       *(uint32_t *)rd);
            }
            return;
        }
    }

    // Zicsr
    if (strcmp(context->instruction, "ecall") == 0 ||
        strcmp(context->instruction, "ebreak") == 0) {
        printf(", %s\n", context->instruction);
        return;
    }

    if (strcmp(context->instruction, "mret") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s\n", context->instruction);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

#if (RVE_E_ZICSR == 1)
    if (strcmp(context->instruction, "_trap") == 0) {
        const char *causedescription = RiscvEmulatorGetMcauseException(
            state->csr.mcause.interrupt,
            state->csr.mcause.exceptioncode);

        printf(", trap, interrupt: %d, exception code %d: %s\n",
               state->csr.mcause.interrupt,
               state->csr.mcause.exceptioncode,
               causedescription);
        printf("%smtval = 0x%08X\n",
               tab,
               state->csr.mtval);
        printf("%smstatus.mpp = %d\n",
               tab,
               state->csr.mstatus.mpp);
        printf("%smstatus.mpie = %d\n",
               tab,
               state->csr.mstatus.mpie);
        printf("%smstatus.mie = %d\n",
               tab,
               state->csr.mstatus.mie);
        printf("%smepc = 0x%08X\n",
               tab,
               state->csr.mepc);
        printf("%spc = 0x%08X\n",
               tab,
               state->programcounternext);

        return;
    }
#endif

    // Zifencei

    // Zba

    // Zbb

    // Zbc

    // Zbs

    // Unkown how to print instruction.
    if (context->hook == HOOK_BEGIN ||
        context->hook == HOOK_UNKNOWN) {
        printf(", ");
    }
    if (context->hook == HOOK_END) {
        printf("%s", tab);
    }
    printf("%s ??? hook %d\n",
           context->instruction,
           context->hook);
}

/**
 * Debug prints for Register-Register Instructions.
 */
void RiscvEmulatorRegRegHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs2num,
    const void *rs2) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);
    const char *rs2name = RiscvEmulatorGetRegisterSymbolicName(rs2num);

#if (RVE_E_C == 1)
    if (state->instruction.copcode.op != OPCODE16_QUADRANT_INVALID) {
        printf("pc: 0x%08X, instruction:     0x%04X, %s, rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
               state->programcounter,
               state->instruction.value,
               instruction,
               rdnum,
               rdname,
               *(uint32_t *)rd,
               rs2num,
               rs2name,
               *(uint32_t *)rs2);
    } else
#endif
    {
        printf("pc: 0x%08X, instruction: 0x%08X, %s, rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X\n",
               state->programcounter,
               state->instruction.value,
               instruction,
               rdnum,
               rdname,
               *(uint32_t *)rd,
               rs2num,
               rs2name,
               *(uint32_t *)rs2);
    }
}

void RiscvEmulatorRegRegHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint8_t rs2num,
    const void *rs2) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    if (rdnum != 0) {
        printf("x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    }
}

/**
 * Debug print for Stack-relative Store Operations.
 */
void RiscvEmulatorStackRelativeStoreHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rs2num,
    const void *rs2,
    const void *sp,
    const uint32_t imm,
    const uint32_t memorylocation) {

    const char *rs2name = RiscvEmulatorGetRegisterSymbolicName(rs2num);

    printf("pc: 0x%08X, instruction:     0x%04X, %s, rs2 x%u(%s): 0x%08X, sp: 0x%08X, imm: 0x%02X(%d), memorylocation: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rs2num,
           rs2name,
           *(uint32_t *)rs2,
           *(uint32_t *)sp,
           (uint8_t)imm,
           imm,
           memorylocation);
}

/**
 * Debug print for Stack-relative Store Operations.
 */
void RiscvEmulatorStackRelativeStoreHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rs2num,
    const void *rs2,
    const void *sp,
    const uint32_t imm,
    const uint32_t memorylocation) {

    printf("                                         0x%08X = 0x%08X\n",
           memorylocation,
           *(uint32_t *)rs2);
}

/**
 * Debug print for Stack-relative Load Operations.
 */
void RiscvEmulatorStackRelativeLoadHookBegin(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const void *sp,
    const uint32_t imm,
    const uint32_t memorylocation) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("pc: 0x%08X, instruction:     0x%04X, %s, rd x%u(%s): 0x%08X, sp: 0x%08X, imm: 0x%02X(%d), memorylocation: 0x%08X\n",
           state->programcounter,
           state->instruction.value,
           instruction,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           *(uint32_t *)sp,
           (uint8_t)imm,
           imm,
           memorylocation);
}

/**
 * Debug print for Stack-relative Load Operations.
 */
void RiscvEmulatorStackRelativeLoadHookEnd(
    const char *instruction,
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const void *sp,
    const uint32_t imm,
    const uint32_t memorylocation) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("x%u(%s) = 0x%08X\n",
           rdnum,
           rdname,
           *(uint32_t *)rd);
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

    // Detect pseudoinstruction BEQZ
    if (strcmp(instruction, "beq") == 0 &&
        rs2num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, beqz, rs1 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
               state->programcounter,
               state->instruction.value,
               rs1num,
               rs1name,
               *(uint32_t *)rs1,
               (uint16_t)imm,
               imm);
        return;
    }

    // Detect pseudoinstruction BNEZ
    if (strcmp(instruction, "bne") == 0 &&
        rs2num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, bnez, rs1 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
               state->programcounter,
               state->instruction.value,
               rs1num,
               rs1name,
               *(uint32_t *)rs1,
               (uint16_t)imm,
               imm);
        return;
    }

    // Detect pseudoinstruction BGEZ
    if (strcmp(instruction, "bge") == 0 &&
        rs2num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, bgez, rs1 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
               state->programcounter,
               state->instruction.value,
               rs1num,
               rs1name,
               *(uint32_t *)rs1,
               (uint16_t)imm,
               imm);
        return;
    }

    // Detect pseudoinstruction BLTZ
    if (strcmp(instruction, "blt") == 0 &&
        rs2num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, bltz, rs1 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
               state->programcounter,
               state->instruction.value,
               rs1num,
               rs1name,
               *(uint32_t *)rs1,
               (uint16_t)imm,
               imm);
        return;
    }

    // Detect pseudoinstruction BLEZ
    if (strcmp(instruction, "bge") == 0 &&
        rs1num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, blez, rs2 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
               state->programcounter,
               state->instruction.value,
               rs2num,
               rs2name,
               *(uint32_t *)rs2,
               (uint16_t)imm,
               imm);
        return;
    }

    // Detect pseudoinstruction BGTZ
    if (strcmp(instruction, "blt") == 0 &&
        rs1num == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, bgtz, rs2 x%u(%s): 0x%08X, imm: 0x%04X(%d)\n",
               state->programcounter,
               state->instruction.value,
               rs2num,
               rs2name,
               *(uint32_t *)rs2,
               (uint16_t)imm,
               imm);
        return;
    }

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

    printf("x%u(%s) = 0x%08X\n",
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
    const uint32_t imm31_12) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    printf("pc: 0x%08X, instruction: 0x%08X, lui, rd x%u(%s): 0x%08X, imm31_12: 0x%05X\n",
           state->programcounter,
           state->instruction.value,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           imm31_12);
}

void RiscvEmulatorLUIHookEnd(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const uint32_t imm31_12) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    if (rdnum != 0) {
        printf("x%u(%s) = 0x%08X\n",
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
    const int32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    // Detect pseudoinstruction J
    if (rdnum == 0) {
        printf("pc: 0x%08X, instruction: 0x%08X, j, imm: 0x%08X(%d)\n",
               state->programcounter,
               state->instruction.value,
               imm,
               imm);
        return;
    }

    printf("pc: 0x%08X, instruction: 0x%08X, jal, rd x%u(%s): 0x%08X, imm: 0x%08X(%d)\n",
           state->programcounter,
           state->instruction.value,
           rdnum,
           rdname,
           *(uint32_t *)rd,
           imm,
           imm);
}

void RiscvEmulatorJALHookEnd(
    const RiscvEmulatorState_t *state,
    const uint8_t rdnum,
    const void *rd,
    const int32_t imm) {

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    if (rdnum != 0) {
        printf("x%u(%s) = 0x%08X\n",
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

    const char *rdname = RiscvEmulatorGetRegisterSymbolicName(rdnum);

    if (rdnum != 0) {
        printf("x%u(%s) = 0x%08X\n",
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

    printf("                                         %s = 0x%08X\n",
           csrname,
           *(uint32_t *)csr);

    if (rdnum != 0) {
        printf("x%u(%s) = 0x%08X\n",
               rdnum,
               rdname,
               *(uint32_t *)rd);
    }
}

#endif

#pragma GCC diagnostic pop