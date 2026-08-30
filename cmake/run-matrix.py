#!/usr/bin/env python3
# Copyright Marc Ketel
# SPDX-License-Identifier: Apache-2.0
#
# Build the full ISA-extension-combination matrix with CMake, using any
# native compiler. PlatformIO-free replacement for `pio run -e <env>`.
#
# Usage:
#   ./run-matrix.py gcc-13              # whole matrix with gcc-13
#   ./run-matrix.py clang               # whole matrix with clang
#   ./run-matrix.py --all               # matrix for every compiler in COMPILERS
#   ./run-matrix.py gcc --only RV32IM RV32IB   # subset of combinations
#   ./run-matrix.py gcc --jobs 8        # limit parallelism (default: nproc)
#
# Per combination a separate CMake build directory (build/<compiler>/<isa>)
# is used; the built program is copied to binaries/<compiler>/<isa> (same
# contract as the PlatformIO copy_binaries.py script, so the ACT wrapper
# keeps working). ccache is used when available.

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

# This script lives in <repo>/cmake/.
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE_DIR = os.path.join(REPO, "cmake")
GENERATOR = os.path.join(REPO, "generate-isa-extension-combination.py")
COMBINATIONS_FILE = os.path.join(CMAKE_DIR, "isa-combinations.cmake")

# Compilers tested by --all.
COMPILERS = ["gcc", "clang"]


def ensure_combinations_file() -> list:
    """Return the combinations list, regenerating the cmake file if needed.

    Each entry is "NAME;-DDEFINE=1;..."."""
    def generate():
        subprocess.run(
            [sys.executable, GENERATOR, "--cmake", COMBINATIONS_FILE],
            check=True,
        )

    if not os.path.exists(COMBINATIONS_FILE):
        generate()
    text = open(COMBINATIONS_FILE).read()
    match = re.search(r"set\(RVE_COMBINATIONS\n(.*?)\n\)", text, re.S)
    if not match:
        # Unparsable or empty: regenerate once, then re-read.
        generate()
        text = open(COMBINATIONS_FILE).read()
        match = re.search(r"set\(RVE_COMBINATIONS\n(.*?)\n\)", text, re.S)
        if not match:
            sys.exit(f"error: cannot parse {COMBINATIONS_FILE}")
    return [line.strip().strip('"') for line in match.group(1).splitlines()]


def parse_combination(entry: str) -> tuple:
    parts = entry.split(";")
    return parts[0], parts[1:]


def build_one(compiler: str, name: str, defines: list, jobs: int) -> tuple:
    """Configure and build one combination. Returns (name, ok, log)."""
    build_dir = os.path.join(REPO, "build", compiler, name)
    ccache = shutil.which("ccache")
    configure = [
        "cmake", "-B", build_dir, "-S", CMAKE_DIR,
        f"-DCMAKE_C_COMPILER={compiler}",
        f"-DRVE_COMPILER_TAG={os.path.basename(compiler)}",
        f"-DRVE_EXTRA_DEFINES={';'.join(defines)}",
        f"-DRVE_BINARY_NAME={name}",
    ]
    if ccache:
        configure.append(f"-DCMAKE_C_COMPILER_LAUNCHER={ccache}")
    build = ["cmake", "--build", build_dir, "--parallel", str(jobs)]

    log = ""
    for cmd in (configure, build):
        proc = subprocess.run(cmd, capture_output=True, text=True)
        log += proc.stdout + proc.stderr
        if proc.returncode != 0:
            return name, False, log
    return name, True, log


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build the ISA-combination matrix with CMake."
    )
    parser.add_argument(
        "compiler", nargs="?", default=None,
        help="compiler to use (name on PATH or full path), e.g. gcc-13",
    )
    parser.add_argument(
        "--all", action="store_true",
        help=f"run the matrix for every compiler in {COMPILERS}",
    )
    parser.add_argument(
        "--only", nargs="+", metavar="ISA",
        help="restrict to these combinations (e.g. RV32IM RV32IB)",
    )
    parser.add_argument(
        "--jobs", type=int, default=os.cpu_count() or 1,
        help="parallel build jobs (default: number of CPUs)",
    )
    args = parser.parse_args()

    compilers = COMPILERS if args.all else [args.compiler]
    if not compilers or compilers == [None]:
        parser.error("give a compiler or use --all")

    combinations = ensure_combinations_file()
    if args.only:
        known = {parse_combination(entry)[0] for entry in combinations}
        unknown = [isa for isa in args.only if isa not in known]
        if unknown:
            sys.exit(f"error: unknown combination(s): {', '.join(unknown)}")
        combinations = [e for e in combinations
                        if parse_combination(e)[0] in args.only]

    tasks = []
    for compiler in compilers:
        for entry in combinations:
            name, defines = parse_combination(entry)
            tasks.append((compiler, name, defines))

    print(f"Building {len(tasks)} combination(s) with "
          f"{', '.join(compilers)} (jobs: {args.jobs})...")
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(build_one, compiler, name, defines, args.jobs): (compiler, name)
            for compiler, name, defines in tasks
        }
        for future in concurrent.futures.as_completed(futures):
            compiler, name = futures[future]
            name, ok, log = future.result()
            status = "OK" if ok else "FAILED"
            print(f"  [{status}] {compiler}/{name}")
            results.append((compiler, name, ok, log))

    failed = [(c, n, log) for c, n, ok, log in results if not ok]
    print(f"\nSummary: {len(results) - len(failed)} succeeded, "
          f"{len(failed)} failed")
    for compiler, name, log in failed:
        print(f"\n=== {compiler}/{name} ===\n{log}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
