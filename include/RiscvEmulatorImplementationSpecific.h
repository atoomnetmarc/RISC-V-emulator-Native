/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <RiscvEmulatorDefine.h>
#include <RiscvEmulatorType.h>

#include "memory.h"

#ifndef RiscvEmulatorImplementationSpecific_H_
#define RiscvEmulatorImplementationSpecific_H_

/**
 * Real-time counter for the `time` CSR.
 *
 * The RISC-V `time` CSR is a free-running real-time counter, not an
 * instruction counter (zicntr.adoc:116). On the Native platform the host
 * clock is the natural source. The unit is nanoseconds; the guest only
 * requires a monotonic-enough counter, not a specific frequency.
 */
static inline uint64_t RiscvEmulatorTime(void) {
    struct timespec ts = {0};
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define RVE_TIME_SOURCE(state) RiscvEmulatorTime()

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
 * @return true when the address is mapped, false otherwise.
 */
static inline bool RiscvEmulatorLoad(uint32_t address, void *destination, uint8_t length) {
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
        return true;
    }

    if (address < RAM_ORIGIN || address >= RAM_ORIGIN + RAM_LENGTH) {
        return false;
    }

    memcpy(destination, &memory[address - RAM_ORIGIN], length);
    return true;
}

/**
 * Stores bytes from RISC-V to emulator.
 *
 * @param address The byte address in memory.
 * @param source The source address to copy the data from.
 * @param length The length in bytes of the data.
 * @return true when the address is mapped, false otherwise.
 */
static inline bool RiscvEmulatorStore(uint32_t address, const void *source, uint8_t length) {
    // UART console output. ACT4 RVMODEL_IO_WRITE_STR writes each character as a
    // word (sw) to 0x10000000. The character is in the low byte. Emit it to stdout.
    if (address == 0x10000000) {
        uint8_t ch;
        memcpy(&ch, source, 1);
        putchar(ch);
        return true;
    }

    // HALT test termination. RVMODEL_HALT_PASS writes TEST_PASS (123456789) to
    // 0x20000000; RVMODEL_HALT_FAIL writes 1. Both end in an infinite self-loop,
    // so stop here.
    if (address == 0x20000000) {
        memcpy(&testresult, source, 4);
        pleasestop = 1;
        return true;
    }

    if (address < RAM_ORIGIN || address >= RAM_ORIGIN + RAM_LENGTH) {
        return false;
    }

    memcpy(&memory[address - RAM_ORIGIN], source, length);
    return true;
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

/**
 * Handles an ECALL.
 *
 * The exit() semihosting convention is only applied when there is no trap
 * handler. A real kernel installs mtvec and expects the ECALL to trap.
 */
static inline void RiscvEmulatorHandleECALL(RiscvEmulatorState_t *state) {
#if (RVE_E_ZICSR == 1)
    if (state->csr.mtvec.base != 0) {
        return;
    }
#endif

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
