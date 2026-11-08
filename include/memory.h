/*

Copyright Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>

#ifndef MEMORY_H_
#define MEMORY_H_

// Size in bytes.
#define RAM_LENGTH 0x4000000

// Reserved at the top of RAM for the device tree blob.
#define DTB_RESERVE 0x4000

// SYSCON / SiFive test finisher memory range.
#define SYSCON_ORIGIN 0x00100000
#define SYSCON_SIZE   0x1000

// Dynamically allocated in main() with RAM_LENGTH bytes.
extern uint8_t *memory;

extern uint8_t pleasestop;

// Test result written by the HALT store hook (0x20000000).
// TEST_PASS means pass; any other value means fail.
#define TEST_PASS 123456789u
extern uint32_t testresult;

// Verbose instruction tracing, enabled with the -v command-line flag.
extern uint8_t verbose;

#endif
