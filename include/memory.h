/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stddef.h>
#include <stdint.h>

#ifndef MEMORY_H_
#define MEMORY_H_

// Size in bytes.
#define RAM_LENGTH 0x4000000

// Reserved at the top of RAM for the device tree blob.
#define DTB_RESERVE 0x4000

// One bit per 4 KiB page of guest RAM.
#define RAM_PAGE_SIZE   0x1000u
#define RAM_PAGE_COUNT  (RAM_LENGTH / RAM_PAGE_SIZE)
#define RAM_DIRTY_BYTES (RAM_PAGE_COUNT / 8u)

// SYSCON / SiFive test finisher memory range.
#define SYSCON_ORIGIN 0x00100000
#define SYSCON_SIZE   0x1000

// Dynamically allocated in main() with RAM_LENGTH bytes.
extern uint8_t *memory;

// One bit per RAM page, set by CPU stores and file loads.
extern uint8_t ram_dirty[RAM_DIRTY_BYTES];

/**
 * Marks the RAM pages covering the given range as touched.
 */
static inline void ram_mark_dirty(uint32_t address, uint32_t length) {
    if (length == 0 || address < RAM_ORIGIN || address >= RAM_ORIGIN + RAM_LENGTH) {
        return;
    }
    uint32_t offset = address - RAM_ORIGIN;
    uint32_t end = offset + length;
    if (end > RAM_LENGTH) {
        end = RAM_LENGTH;
    }
    uint32_t first = offset / RAM_PAGE_SIZE;
    uint32_t last = (end - 1u) / RAM_PAGE_SIZE;
    for (uint32_t page = first; page <= last; page++) {
        ram_dirty[page / 8u] |= (uint8_t)(1u << (page % 8u));
    }
}

/**
 * Returns the number of RAM pages touched at least once.
 */
static inline size_t ram_dirty_pages(void) {
    size_t count = 0;
    for (size_t i = 0; i < RAM_DIRTY_BYTES; i++) {
        uint8_t byte = ram_dirty[i];
        for (uint8_t bit = 0; bit < 8u; bit++) {
            if ((byte & (uint8_t)(1u << bit)) != 0) {
                count++;
            }
        }
    }
    return count;
}

extern uint8_t pleasestop;

// Test result written by the HALT store hook (0x20000000).
// TEST_PASS means pass; any other value means fail.
#define TEST_PASS 123456789u
extern uint32_t testresult;

// Verbose instruction tracing, enabled with the -v command-line flag.
extern uint8_t verbose;

#endif
