# CMake

PlatformIO-free build with pluggable compilers. Requires CMake ≥ 3.25 and a
C2x-capable compiler; uses ccache when available. All paths below are relative
to the repository root.

## Build one combination

```
cmake -B build/gcc/RV32I -S cmake -DCMAKE_C_COMPILER=gcc-13 -DRVE_EXTRA_DEFINES=-DRVE_E_M=1
cmake --build build/gcc/RV32I
```

Binary: `binaries/<compiler-tag>/<name>`, listing next to it in the build dir.
Relevant cache variables: `RVE_EMULATOR_DIR` (default `../RISC-V-emulator`),
`RVE_EXTRA_DEFINES` (`;`-separated `-D` flags), `RVE_COMPILER_TAG`,
`RVE_BINARY_NAME`.

## Full matrix

[run-matrix.py](run-matrix.py) builds every combination of
[isa-combinations.cmake](isa-combinations.cmake) (generated, see below), one
build dir per combination, in parallel, with a summary and non-zero exit on
failure:

```
cmake/run-matrix.py gcc-13            # whole matrix with one compiler
cmake/run-matrix.py --all             # every compiler in COMPILERS (gcc, clang)
cmake/run-matrix.py gcc --only RV32IM RV32IB
cmake/run-matrix.py gcc --jobs 8
```

## Regenerate combinations

```
python3 generate-isa-extension-combination.py --cmake cmake/isa-combinations.cmake
```

`run-matrix.py` regenerates this file automatically when missing or stale.
GCC-only flags (`-fno-reorder-blocks`, `-Wno-packed-bitfield-compat`) are
filtered per compiler via `check_c_compiler_flag()`; no per-compiler config
needed.
