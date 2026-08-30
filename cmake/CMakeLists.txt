# Copyright Marc Ketel
# SPDX-License-Identifier: Apache-2.0
#
# Native build of the RISC-V emulator host program.
#
# Usage (see run-matrix.py for the full ISA-combination matrix):
#   cmake -B build/RV32I -S cmake -DCMAKE_C_COMPILER=gcc-13
#   cmake --build build/RV32I
#
# The emulator library is header-only and expected next to this repository;
# override with -DRVE_EMULATOR_DIR=/path/to/RISC-V-emulator.
cmake_minimum_required(VERSION 3.25)
project(RiscvEmulatorNative C)

# Repository root (this file lives in cmake/).
set(RVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(RVE_EMULATOR_DIR "${RVE_ROOT}/../RISC-V-emulator"
    CACHE PATH "Path to the RISC-V-emulator repository")

add_executable(program
  "${RVE_ROOT}/src/main.c"
  "${RVE_ROOT}/src/hook.c"
)

target_include_directories(program PRIVATE
  "${RVE_ROOT}/include"
  "${RVE_ROOT}/src"
  "${RVE_EMULATOR_DIR}/include"
  "${RVE_EMULATOR_DIR}"
)

# C2x standard (CMake knows this as c_std_23; fall back to the raw flag).
include(CheckCCompilerFlag)
check_c_compiler_flag(-std=c2x RVE_HAS_C2X)
if(RVE_HAS_C2X)
  target_compile_options(program PRIVATE -std=c2x)
endif()

set(RVE_EXTRA_DEFINES ""
    CACHE STRING "Additional emulator defines, e.g. -DRVE_E_M=1;-DRVE_E_ZBA=1")
separate_arguments(RVE_EXTRA_DEFINES)
target_compile_definitions(program PRIVATE
  RVE_E_HOOK=1
  RVE_E_MISALIGNED=1
  ${RVE_EXTRA_DEFINES}
)

# Flags replicated from the PlatformIO [common] build_flags. GCC-only flags
# are filtered out automatically for other compilers (e.g. clang).
include(CheckCCompilerFlag)

target_compile_options(program PRIVATE
  -Og
  -g3
  -Wall
  -Wextra
  -Werror
  -Wpedantic
)

# -fno-reorder-blocks: GCC-only.
check_c_compiler_flag(-fno-reorder-blocks RVE_HAS_NO_REORDER_BLOCKS)
if(RVE_HAS_NO_REORDER_BLOCKS)
  target_compile_options(program PRIVATE -fno-reorder-blocks)
endif()

# -mno-ms-bitfields: GCC-only (struct layout; clang accepts it, other
# compilers may not).
check_c_compiler_flag(-mno-ms-bitfields RVE_HAS_NO_MS_BITFIELDS)
if(RVE_HAS_NO_MS_BITFIELDS)
  target_compile_options(program PRIVATE -mno-ms-bitfields)
endif()

# -Wno-packed-bitfield-compat: GCC-only; clang uses -Wno-padded-bitfield.
check_c_compiler_flag(-Wno-packed-bitfield-compat RVE_HAS_NO_PACKED_BITFIELD_COMPAT)
if(RVE_HAS_NO_PACKED_BITFIELD_COMPAT)
  target_compile_options(program PRIVATE -Wno-packed-bitfield-compat)
else()
  check_c_compiler_flag(-Wno-padded-bitfield RVE_HAS_NO_PADDED_BITFIELD)
  if(RVE_HAS_NO_PADDED_BITFIELD)
    target_compile_options(program PRIVATE -Wno-padded-bitfield)
  endif()
endif()

# Listing, same output as the PlatformIO lss.py extra script.
add_custom_command(TARGET program POST_BUILD
  COMMAND objdump -h -S "$<TARGET_FILE:program>" > "$<TARGET_FILE:program>.lss"
  VERBATIM
)

# Copy the binary to binaries/<compiler-tag>/<name> with a temporary name +
# rename, same contract (and atomicity) as the PlatformIO copy_binaries.py
# extra script. run-matrix.py sets the tag via -DRVE_COMPILER_TAG=<compiler>
# and the name via -DRVE_BINARY_NAME=<isa>.
set(RVE_COMPILER_TAG "gcc" CACHE STRING "Compiler tag used in binaries/<tag>/")
set(RVE_BINARY_NAME "program" CACHE STRING "Name used for binaries/<tag>/ output")
set(RVE_BINARIES_DIR "${RVE_ROOT}/binaries/${RVE_COMPILER_TAG}")
add_custom_command(TARGET program POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RVE_BINARIES_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:program>"
          "${RVE_BINARIES_DIR}/${RVE_BINARY_NAME}.tmp"
  COMMAND ${CMAKE_COMMAND} -E rename
          "${RVE_BINARIES_DIR}/${RVE_BINARY_NAME}.tmp"
          "${RVE_BINARIES_DIR}/${RVE_BINARY_NAME}"
  VERBATIM
)
