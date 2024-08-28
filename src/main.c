/*

Copyright 2023-2024 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <RiscvEmulator.h>

#include "memory.h"

uint8_t memory[RAM_LENGTH];
uint8_t firmware[RAM_LENGTH];

RiscvEmulatorState_t RiscvEmulatorState;

size_t loopcounter = 0;

int main() {
    pleasestop = 0;

    // For debugging specific test.
    /*
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    chdir("/home/marc/Projects/RISC-V-emulator/RISC-V-emulator-RISCOF/riscof_work/rv32i_m/privilege/src/misalign-lh-01.S/dut");
#pragma GCC diagnostic pop
    */

    printf("Reading dut-ram.bin\n");
    FILE *fram = fopen("dut-ram.bin", "r");
    if (fram == NULL) {
        printf("file not found.\n");
        return 1;
    }
    size_t ramsize = fread(memory, sizeof(uint8_t), sizeof(memory), fram);
    printf("Read %zu bytes.\n", ramsize);
    fclose(fram);

    printf("Reading dut-rom.bin\n");
    FILE *from = fopen("dut-rom.bin", "r");
    if (from == NULL) {
        printf("file not found.\n");
        return 2;
    }
    size_t romsize = fread(firmware, sizeof(uint8_t), sizeof(firmware), from);
    size_t maxloopcounter = (romsize / 4) * 5;
    printf("Read %zu bytes.\n", romsize);
    fclose(from);

    printf("Parsing dut-ram-signature_begin_end.txt\n");
    FILE *fsignature = fopen("dut-ram-signature_begin_end.txt", "r");
    if (fsignature == NULL) {
        printf("file not found.\n");
        return 2;
    }
    char ssignaturebegin[20];
    char *resultbegin;
    char ssignatureend[20];
    char *resultend;
    resultbegin = fgets(ssignaturebegin, sizeof(ssignaturebegin), fsignature);
    if (resultbegin == NULL) {
        printf("reading 1st line of dut-ram-signature_begin_end.txt failed.\n");
        return 3;
    }
    resultend = fgets(ssignatureend, sizeof(ssignatureend), fsignature);
    if (resultend == NULL) {
        printf("reading 2nd line of dut-ram-signature_begin_end.txt failed.\n");
        return 3;
    }
    uint32_t signaturebegin = strtol(ssignaturebegin, NULL, 16);
    uint32_t signatureend = strtol(ssignatureend, NULL, 16);

    if (signaturebegin > signatureend) {
        uint32_t t = signatureend;
        signatureend = signaturebegin;
        signaturebegin = t;
    }

    printf("Signature in RAM between 0x%08x 0x%08x.\n", signaturebegin, signatureend);
    fclose(fsignature);

    printf("RiscvEmulatorInit()\n");

    RiscvEmulatorInit(&RiscvEmulatorState, sizeof(memory));

    for (;;) {
        loopcounter++;
        RiscvEmulatorLoop(&RiscvEmulatorState);

        // If this prints then consider adding a hook in RiscvEmulatorHook.h and implementing it in hook.c.
        if (RiscvEmulatorState.hookexists == 0) {
            printf("pc: 0x%08x, instruction: 0x%08x, ???\n",
                   RiscvEmulatorState.programcounter,
                   RiscvEmulatorState.instruction.value);

            // pleasestop = 1;
        }

        if (RiscvEmulatorState.registers.name.x0 != 0) {
            printf("Error: x0 must always be zero. x0 is now 0x%08x. Stop emulation.\n",
                   RiscvEmulatorState.registers.name.x0);
            pleasestop = 1;
        }

        if (loopcounter >= maxloopcounter) {
            printf("Loopcounter limit reached, stopping emulation.\n");
            break;
        }

        if (pleasestop > 0) {
            break;
        }
    }

    printf("Writing dut-ram-after.bin\n");
    FILE *framafter = fopen("dut-ram-after.bin", "wb");
    size_t writtenbytesframatfer = fwrite(memory, sizeof(uint8_t), ramsize, framafter);
    printf("Wrote %zu bytes.\n", writtenbytesframatfer);
    fclose(framafter);

    printf("Writing DUT-rve.signature\n");
    FILE *fsignature2 = fopen("DUT-rve.signature", "wb");

    for (uint32_t i = signaturebegin; i < signatureend; i += 4) {
        uint32_t ramvalue;
        memcpy(&ramvalue, &memory[i - RAM_ORIGIN], sizeof(ramvalue));
        fprintf(fsignature2, "%08x\n", ramvalue);
    }
    fclose(fsignature2);

    printf("Simulated %zu CPU instructions.\n", loopcounter);
    printf("Exiting.\n");
    return 0;
}
