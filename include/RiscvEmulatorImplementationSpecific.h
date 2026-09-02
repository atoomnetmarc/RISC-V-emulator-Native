/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdbool.h>
#include <string.h>

#include <RiscvEmulatorDefine.h>
#include <RiscvEmulatorType.h>

#include "memory.h"

#ifndef RiscvEmulatorImplementationSpecific_H_
#define RiscvEmulatorImplementationSpecific_H_

// The UART region implements the 16550-style receive path used by the
// RISC-V-emulator-MicroPython port: a byte load at the receive buffer
// returns the oldest received byte, or 0x00 when the FIFO is empty (never
// blocks), and a byte load at the line status register reports data-ready.
#define UART_ORIGIN 0x10000000

#define UART_RECEIVE_BUFFER_OFFSET 0
#define UART_LINE_STATUS_OFFSET    5

#define UART_FIFO_BUFFER_SIZE 16

typedef struct {
    uint8_t buffer[UART_FIFO_BUFFER_SIZE];
    uint8_t write_index;
    uint8_t read_index;
} uart_fifo_t;

static uart_fifo_t uart_rx_fifo = {0};

/**
 * Union of all the ways the UART line status register can be accessed.
 *
 * The bit layout matches the 16550 LSR. The transmitter empty bits are set
 * permanently (output is instantaneous) and the data-ready bit is set
 * whenever the receive FIFO is not empty.
 */
typedef union {
    uint8_t value;
    struct {
        /**
         * Data ready: a received byte is available in the receive FIFO.
         */
        uint8_t dataReady : 1;

        /**
         * Overrun error. Never set.
         */
        uint8_t overrunError : 1;

        /**
         * Parity error. Never set.
         */
        uint8_t parityError : 1;

        /**
         * Framing error. Never set.
         */
        uint8_t framingError : 1;

        /**
         * Break interrupt. Never set.
         */
        uint8_t breakInterrupt : 1;

        /**
         * Transmitter holding register empty. Always set.
         */
        uint8_t transmitterHoldingEmpty : 1;

        /**
         * Transmitter shift register empty. Always set.
         */
        uint8_t transmitterShiftEmpty : 1;
    } bits;
} uart_line_status_register_t;

/**
 * Pushes one received byte into the RX FIFO; drops it on overflow.
 */
static void uart_rx_push(uint8_t data) {
    uint8_t index_next = (uart_rx_fifo.write_index + 1) % sizeof(uart_rx_fifo.buffer);

    if (index_next != uart_rx_fifo.read_index) {
        uart_rx_fifo.buffer[uart_rx_fifo.write_index] = data;
        uart_rx_fifo.write_index = index_next;
    }
}

/**
 * Returns true when the RX FIFO is empty.
 */
static bool uart_rx_empty(void) {
    return uart_rx_fifo.read_index == uart_rx_fifo.write_index;
}

/**
 * Pops the oldest byte from the RX FIFO, or returns 0x00 when it is empty.
 */
static uint8_t uart_rx_char_nonblocking(void) {
    if (uart_rx_empty()) {
        return 0;
    }

    uint8_t data = uart_rx_fifo.buffer[uart_rx_fifo.read_index];
    uart_rx_fifo.read_index = (uart_rx_fifo.read_index + 1) % sizeof(uart_rx_fifo.buffer);

    return data;
}

/**
 * Loads bytes from emulator to RISC-V.
 *
 * @param address The byte address in memory.
 * @param destination The destination address to copy the data to.
 * @param length The length in bytes of the data.
 */
static inline void RiscvEmulatorLoad(uint32_t address, void *destination, uint8_t length) {
    if (address >= UART_ORIGIN && address < RAM_ORIGIN) {
        uint32_t offset = address - UART_ORIGIN;
        if (offset == UART_RECEIVE_BUFFER_OFFSET && length == 1) {
            *(uint8_t *)destination = uart_rx_char_nonblocking();
        } else if (offset == UART_LINE_STATUS_OFFSET && length == 1) {
            uart_line_status_register_t lineStatus = {
                .bits = {
                    .dataReady = !uart_rx_empty(),
                    .transmitterHoldingEmpty = 1,
                    .transmitterShiftEmpty = 1,
                },
            };
            *(uint8_t *)destination = lineStatus.value;
        } else {
            // Unimplemented UART registers read as zero.
            memset(destination, 0, length);
        }
        return;
    }

    if (address < RAM_ORIGIN || address >= RAM_ORIGIN + RAM_LENGTH) {
        printf("Loading from outside RAM does not work. Stopping emulation.\n");
        pleasestop = 1;
    } else {
        memcpy(destination, &memory[address - RAM_ORIGIN], length);
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
    // UART console output. ACT4 RVMODEL_IO_WRITE_STR writes each character as a
    // word (sw) to 0x10000000. The character is in the low byte. Emit it to stdout.
    if (address == 0x10000000) {
        uint8_t ch;
        memcpy(&ch, source, 1);
        putchar(ch);
        return;
    }

    // HALT test termination. RVMODEL_HALT_PASS writes 123456789 to 0x20000000;
    // RVMODEL_HALT_FAIL writes 1. Both end in an infinite self-loop, so stop here.
    if (address == 0x20000000) {
        memcpy(&testresult, source, 4);
        pleasestop = 1;
        return;
    }

    if (address >= RAM_ORIGIN + RAM_LENGTH) {
        printf("Writing to address after RAM will not work. Stopping emulation.\n");
        pleasestop = 1;
    } else if (address >= RAM_ORIGIN) {
        memcpy(&memory[address - RAM_ORIGIN], source, length);
    } else {
        printf("Writing to outside RAM does not work.\n");
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
