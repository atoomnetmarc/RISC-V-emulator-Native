/*

Copyright 2023-2024 Marc Ketel
SPDX-License-Identifier: Apache-2.0

*/

#include <stdint.h>

#ifndef MEMORY_H_
#define MEMORY_H_

// Size in bytes.
#define RAM_LENGTH 0x1000000

uint8_t memory[RAM_LENGTH];
uint8_t firmware[RAM_LENGTH];

uint8_t pleasestop;

#endif
