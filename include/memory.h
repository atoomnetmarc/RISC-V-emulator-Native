/*

Copyright 2023-2025 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>

#ifndef MEMORY_H_
#define MEMORY_H_

// Size in bytes.
#define RAM_LENGTH 0x1000000

uint8_t memory[RAM_LENGTH];

uint8_t pleasestop;

// Test result written by the HALT store hook (0x20000000).
// 123456789 means pass; any other value means fail.
extern uint32_t testresult;

#endif
