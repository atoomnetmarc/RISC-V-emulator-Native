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
    const uint16_t csrnum = context->csrnum;
    const void *csr = context->csr;
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

    const void *ra = &state->registers.symbolic.ra;
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

    /**
     * R-type pseudoinstructions.
     */

    // Detect neg
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

    // Detect snez
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

    // Detect sltz
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

    // Detect sgtz
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

    /**
     * R-type, Integer Register-Register instructions.
     *
     * prints rd, rs1 and rs2.
     */

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

    /**
     * I-type and Compressed Immediate pseudoinstructions.
     */

    // Detect nop
    if (strcmp(context->instruction, "addi") == 0 &&
        rdnum == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", nop\n");
            return;
        } else if (context->hook == HOOK_END) {
            return;
        }
    }

    // Detect mv
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

    // Detect not
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

    // Detect seqz
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

    // Detect c.nop
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

    // Detect ret
    if (strcmp(context->instruction, "jalr") == 0 &&
        rdnum == 0 && rs1num == 1 && imm == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", ret, rs1 x%u(%s): 0x%08X\n",
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        }
    }

    // Detect jr
    if (strcmp(context->instruction, "jalr") == 0 &&
        rdnum == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", jr, rs1 x%u(%s): 0x%08X",
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    /**
     * I-type, Integer Register-Immediate instructions.
     *
     * prints rd, rs1 and imm.
     */

    if (strcmp(context->instruction, "jalr") == 0) {
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
                printf("%spc = 0x%08X\n",
                       tab,
                       state->programcounternext);
            }
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

    /**
     * I-type Load instructions.
     *
     * prints rd, rs1, imm and memory location.
     */

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

    /**
     * I-type Misc-Mem instructions.
     */

    if (strcmp(context->instruction, "fence") == 0 ||
        strcmp(context->instruction, "fencei") == 0) {
        printf(", %s\n", context->instruction);
        return;
    }

    /**
     * I-type System instructions.
     */

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

    if (strcmp(context->instruction, "ecall") == 0 ||
        strcmp(context->instruction, "ebreak") == 0) {
        printf(", %s\n", context->instruction);
        return;
    }

#if (RVE_E_ZICSR == 1)
    if (strcmp(context->instruction, "csrrw") == 0 ||
        strcmp(context->instruction, "csrrs") == 0 ||
        strcmp(context->instruction, "csrrc") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, rs1 x%u(%s): 0x%08X, csr 0x%04X(%s): 0x%08X\n",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1,
                   csrnum,
                   csrname,
                   *(uint32_t *)csr);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%s%s = 0x%08X\n",
                   tab,
                   csrname,
                   *(uint32_t *)csr);
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

    if (strcmp(context->instruction, "csrrwi") == 0 ||
        strcmp(context->instruction, "csrrsi") == 0 ||
        strcmp(context->instruction, "csrrci") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            printInteger(immname, imm, immlength, immissigned);
            printf(", csr 0x%04X(%s): 0x%08X\n",
                   csrnum,
                   csrname,
                   *(uint32_t *)csr);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%s%s = 0x%08X\n",
                   tab,
                   csrname,
                   *(uint32_t *)csr);
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
#endif

    /**
     * S-type, Compressed Store instructions.
     *
     * prints rs1, rs2, imm and memory location.
     */

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

    /**
     * B-type pseudoinstructions.
     */

    // Detect BEQZ
    if (strcmp(context->instruction, "beq") == 0 &&
        rs2num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", beqz, rs1 x%u(%s): 0x%08X",
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    // Detect BNEZ
    if (strcmp(context->instruction, "bne") == 0 &&
        rs2num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", bnez, rs1 x%u(%s): 0x%08X",
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    // Detect BGEZ
    if (strcmp(context->instruction, "bge") == 0 &&
        rs2num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", bgez, rs1 x%u(%s): 0x%08X",
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    // Detect BLTZ
    if (strcmp(context->instruction, "blt") == 0 &&
        rs2num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", bltz, rs1 x%u(%s): 0x%08X",
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    // Detect BLEZ
    if (strcmp(context->instruction, "bge") == 0 &&
        rs1num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", blez, rs2 x%u(%s): 0x%08X",
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    // Detect BGTZ
    if (strcmp(context->instruction, "blt") == 0 &&
        rs1num == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", bgtz, rs2 x%u(%s): 0x%08X",
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        }
    }

    /**
     * B-type instructions.
     *
     * prints rs1, rs2, and imm.
     */

    if (strcmp(context->instruction, "beq") == 0 ||
        strcmp(context->instruction, "bne") == 0 ||
        strcmp(context->instruction, "bge") == 0 ||
        strcmp(context->instruction, "bgeu") == 0 ||
        strcmp(context->instruction, "blt") == 0 ||
        strcmp(context->instruction, "bltu") == 0) {
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
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

    /**
     * U-type instructions.
     *
     * prints rd and imm.
     */

    if (strcmp(context->instruction, "lui") == 0 ||
        strcmp(context->instruction, "auipc") == 0) {
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

    /**
     * J-type pseudoinstructions.
     */

    // Detect J
    if (strcmp(context->instruction, "jal") == 0) {
        if (rdnum == 0) {
            if (context->hook == HOOK_BEGIN) {
                printf(", j");
                printInteger(immname, imm, immlength, immissigned);
                printf("\n");
                return;
            }
        }
    }

    /**
     * J-type instructions.
     *
     * prints rd and imm.
     */

    if (strcmp(context->instruction, "jal") == 0) {
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
                printf("%spc = 0x%08X\n",
                       tab,
                       state->programcounternext);
            }
            return;
        }
    }

    /**
     * Compressed Register instructions.
     * Compressed Arithmetic instructions.
     */

    if (strcmp(context->instruction, "c.add") == 0 ||
        strcmp(context->instruction, "c.sub") == 0 ||
        strcmp(context->instruction, "c.xor") == 0 ||
        strcmp(context->instruction, "c.or") == 0 ||
        strcmp(context->instruction, "c.and") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rs1/rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            printf("%sx%u(%s) = 0x%08X\n",
                   tab,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            return;
        }
    }

    if (strcmp(context->instruction, "c.mv") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, rs2 x%u(%s): 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            printf("%sx%u(%s) = 0x%08X\n",
                   tab,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            return;
        }
    }

    if (strcmp(context->instruction, "c.jr") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rs1 x%u(%s): 0x%08X\n",
                   context->instruction,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

    if (strcmp(context->instruction, "c.jalr") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, x1(ra): 0x%08X, rs1 x%u(%s): 0x%08X\n",
                   context->instruction,
                   *(uint32_t *)ra,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%sx1(ra) = 0x%08X\n",
                   tab,
                   *(uint32_t *)ra);
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

    /**
     * Compressed Immediate instructions.
     */

    if (strcmp(context->instruction, "c.li") == 0 ||
        strcmp(context->instruction, "c.lui") == 0 ||
        strcmp(context->instruction, "c.addi") == 0 ||
        strcmp(context->instruction, "c.addi16sp") == 0 ||
        strcmp(context->instruction, "c.slli") == 0) {
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
            printf("%sx%u(%s) = 0x%08X\n",
                   tab,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            return;
        }
    }

    /**
     * Compressed Stack-relative Store instructions.
     */

    if (strcmp(context->instruction, "c.swsp") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rs2 x%u(%s): 0x%08X, sp: 0x%08X",
                   context->instruction,
                   rs2num,
                   rs2name,
                   *(uint32_t *)rs2,
                   *(uint32_t *)sp);
            printInteger(immname, imm, immlength, immissigned);
            printf(", memorylocation: 0x%08X\n",
                   memorylocation);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%s0x%08X = 0x%08X\n",
                   tab,
                   memorylocation,
                   *(uint32_t *)rs2);
            return;
        }
    }

    /**
     * Compressed Wide Immediate instructions.
     */

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

    /**
     * Compressed Load instructions.
     */

    if (strcmp(context->instruction, "c.lwsp") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rd x%u(%s): 0x%08X, sp: 0x%08X",
                   context->instruction,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd,
                   *(uint32_t *)sp);
            printInteger(immname, imm, immlength, immissigned);
            printf(", memorylocation: 0x%08X\n",
                   memorylocation);
            return;
        } else if (context->hook == HOOK_END) {
            printf("%sx%u(%s) = 0x%08X\n",
                   tab,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            return;
        }
    }

    if (strcmp(context->instruction, "c.lw") == 0) {
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
            printf("%sx%u(%s) = 0x%08X\n",
                   tab,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            return;
        }
    }

    /**
     * Compressed Branch instructions.
     */

    if (strcmp(context->instruction, "c.srli") == 0 ||
        strcmp(context->instruction, "c.srai") == 0 ||
        strcmp(context->instruction, "c.andi") == 0) {
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
            printf("%sx%u(%s) = 0x%08X\n",
                   tab,
                   rdnum,
                   rdname,
                   *(uint32_t *)rd);
            return;
        }
    }

    if (strcmp(context->instruction, "c.beqz") == 0 ||
        strcmp(context->instruction, "c.bnez") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, rs1 x%u(%s): 0x%08X",
                   context->instruction,
                   rs1num,
                   rs1name,
                   *(uint32_t *)rs1);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

    /**
     * Compressed Jump instructions.
     */

    if (strcmp(context->instruction, "c.jal") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s, x1(ra): 0x%08X",
                   context->instruction,
                   *(uint32_t *)ra);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            printf("%sx1(ra) = 0x%08X\n",
                   tab,
                   *(uint32_t *)ra);
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

    if (strcmp(context->instruction, "c.j") == 0) {
        if (context->hook == HOOK_BEGIN) {
            printf(", %s",
                   context->instruction);
            printInteger(immname, imm, immlength, immissigned);
            printf("\n");
            return;
        } else if (context->hook == HOOK_END) {
            printf("%spc = 0x%08X\n",
                   tab,
                   state->programcounternext);
            return;
        }
    }

    /**
     * Not instructions. Like trap.
     */

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

    /**
     * Fallback prints. When hitting this, please add the instruction somewhere above.
     */

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

#pragma GCC diagnostic pop