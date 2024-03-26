#include <stdio.h>

#include <RiscvEmulator.h>

#include "memory.h"

uint8_t memory[RAM_LENGTH];
uint8_t firmware[RAM_LENGTH];

RiscvEmulatorState_t RiscvEmulatorState;

size_t loopcounter = 0;

int main() {
    pleasestop = 0;

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
    size_t rominstruction = fread(firmware, sizeof(uint8_t), sizeof(firmware), from) / 4;
    fclose(from);

    printf("RiscvEmulatorInit()\n");

    RiscvEmulatorInit(&RiscvEmulatorState, sizeof(memory));

    for (;;) {
        loopcounter++;
        RiscvEmulatorLoop(&RiscvEmulatorState);

        //printf("pc: 0x%08x, instruction: 0x%08x\n", RiscvEmulatorState.programcounter, RiscvEmulatorState.instruction.value);

        if (loopcounter > rominstruction * 5) {
            printf("Loopcounter limit reached, stopping emulation.\n");
            break;
        }

        if (pleasestop > 0) {
            break;
        }
    }

    printf("Writing dut-ram-after.bin\n");
    FILE *framafter = fopen("dut-ram-after.bin", "wb");
    size_t writtenbytes = fwrite(memory, sizeof(uint8_t), ramsize, framafter);
    printf("Wrote %zu bytes.\n", writtenbytes);
    fclose(framafter);

    printf("Simulated %zu CPU instructions.\n", loopcounter);
    printf("Exiting.\n");
    return 0;
}
