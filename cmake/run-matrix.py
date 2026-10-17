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
import json
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

# Compilers tested by --all. Compilers not found on PATH are skipped
# (reported in the summary). Entries of the form "docker:<image>" run the
# build inside that container image (e.g. docker:gcc:14 uses the official
# docker.io/library/gcc:14 image, which ships gcc 14); the image must be
# pulled beforehand, otherwise the entry is skipped like any missing
# compiler.
COMPILERS = ["gcc", "clang", "tcc", "icx",
             "docker:gcc:12",
             "docker:gcc:13",
             "docker:gcc:14",
             "docker:gcc:15",
             "docker:gcc:16"]

# Compilers that ccache does not support
NO_CCACHE = {"tcc"}

# Environment setup script sourced automatically when icx is requested but
# not yet on PATH.
ONEAPI_SETVARS = "/opt/intel/oneapi/setvars.sh"


def parse_compiler(spec: str) -> dict:
    """Split a compiler spec into its components.

    "gcc-13"        -> host compiler gcc-13, tag "gcc-13"
    "docker:gcc:13" -> the image's default gcc inside docker.io/library/gcc:13,
                       tag "gcc-13" (same layout as a host gcc-13 build)
    """
    if spec.startswith("docker:"):
        image = spec[len("docker:"):]           # e.g. "gcc:13"
        return {"docker": True, "image": image, "compiler": "gcc",
                "tag": image.replace(":", "-")}
    return {"docker": False, "compiler": spec, "tag": os.path.basename(spec)}


def compiler_available(spec: str) -> bool:
    """Return True when the compiler (or container image) can be used."""
    info = parse_compiler(spec)
    if not info["docker"]:
        return shutil.which(info["compiler"]) is not None
    if not shutil.which("docker"):
        return False
    proc = subprocess.run(
        ["docker", "image", "inspect", info["image"]],
        capture_output=True,
    )
    return proc.returncode == 0


def ensure_docker_image(info: dict) -> bool:
    """Make sure a container image with cmake exists for this compiler.

    The official gcc images ship a compiler but no cmake, so a derived
    image (rve-matrix:<tag>) is built once on demand and reused after
    that. Returns False when the image cannot be prepared.
    """
    tag = f"rve-matrix:{info['tag']}"
    if subprocess.run(["docker", "image", "inspect", tag],
                      capture_output=True).returncode == 0:
        info["image"] = tag
        return True

    apt_install = ("apt-get install -y --no-install-recommends cmake \\\n"
                   " && rm -rf /var/lib/apt/lists/*")
    # Older images (e.g. bullseye-based gcc:11) point at EOL apt repos
    # that 404; retry against archive.debian.org in that case.
    variants = [
        f"FROM {info['image']}\nRUN apt-get update \\\n && {apt_install}\n",
        (f"FROM {info['image']}\n"
         "RUN sed -i -e 's|http://deb.debian.org|http://archive.debian.org|g'"
         " -e 's|http://security.debian.org|http://archive.debian.org|g'"
         " /etc/apt/sources.list 2>/dev/null || true \\\n"
         " && apt-get -o Acquire::Check-Valid-Until=false update \\\n"
         f" && {apt_install}\n"),
    ]

    tmpdir = os.path.join(REPO, "build", "docker")
    os.makedirs(tmpdir, exist_ok=True)
    path = os.path.join(tmpdir, f"Dockerfile.{info['tag']}")
    for dockerfile in variants:
        with open(path, "w") as f:
            f.write(dockerfile)
        proc = subprocess.run(
            ["docker", "build", "-q", "-t", tag, "-f", path, tmpdir],
            capture_output=True, text=True,
        )
        if proc.returncode == 0:
            info["image"] = tag
            return True
    print(f"error: docker build for {info['image']} failed:\n"
          f"{proc.stdout}{proc.stderr}", end="")
    return False


def ensure_oneapi_env(compilers: list) -> bool:
    """Source the Intel oneAPI environment when icx is used but not on PATH.

    Returns True when icx is usable (already on PATH or made available by
    sourcing the oneAPI setvars script); False otherwise, so the caller can
    skip icx like any other missing compiler.
    """
    if not any(os.path.basename(c) == "icx" for c in compilers):
        return True
    if shutil.which("icx"):
        return True
    if not os.path.exists(ONEAPI_SETVARS):
        print(f"Skipping icx: not found and {ONEAPI_SETVARS} does not exist")
        return False
    proc = subprocess.run(
        ["bash", "-c",
         f'source "{ONEAPI_SETVARS}" >/dev/null 2>&1 && '
         'python3 -c "import os, json; print(json.dumps(dict(os.environ)))"'],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        print(f"Skipping icx: sourcing {ONEAPI_SETVARS} failed:\n{proc.stderr}")
        return False
    os.environ.update(json.loads(proc.stdout))
    return shutil.which("icx") is not None


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
    info = parse_compiler(compiler)
    if info["docker"]:
        # Use the derived image with cmake (prepared by ensure_docker_image)
        # when it exists, otherwise the base image as-is.
        derived = f"rve-matrix:{info['tag']}"
        if subprocess.run(["docker", "image", "inspect", derived],
                          capture_output=True).returncode == 0:
            info["image"] = derived
        # Build inside the container with the repo mounted at /src. The
        # build directory matches the host layout (build/<tag>/<isa>).
        build_dir = f"build/{info['tag']}/{name}"
        # The defines are ';'-separated for CMake; quote the argument so
        # sh does not interpret the semicolons as command separators.
        inner = (
            f"rm -rf {build_dir} && "
            f"cmake -B {build_dir} -S cmake "
            f"-DCMAKE_C_COMPILER={info['compiler']} "
            f"-DRVE_COMPILER_TAG={info['tag']} "
            f"-DRVE_EXTRA_DEFINES='{';'.join(defines)}' "
            f"-DRVE_BINARY_NAME={name} "
            f"&& cmake --build {build_dir} --parallel {jobs}"
        )
        # Mount the parent directory: the emulator library lives in
        # ../RISC-V-emulator, next to this repository.
        parent = os.path.dirname(REPO)
        workdir = f"/work/{os.path.basename(REPO)}"
        cmd = ["docker", "run", "--rm",
               "-v", f"{parent}:/work", "-w", workdir,
               info["image"], "sh", "-c", inner]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        return name, proc.returncode == 0, proc.stdout + proc.stderr

    build_dir = os.path.join(REPO, "build", compiler, name)
    ccache = (shutil.which("ccache")
              if os.path.basename(compiler) not in NO_CCACHE else None)
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
    if not ensure_oneapi_env(compilers):
        compilers = [c for c in compilers if os.path.basename(c) != "icx"]
    if args.all:
        missing = [c for c in compilers if not compiler_available(c)]
        if missing:
            print(f"Skipping unavailable compiler(s): {', '.join(missing)}")
        compilers = [c for c in compilers if compiler_available(c)]
        if not compilers:
            sys.exit("error: none of the --all compilers were found on PATH")

    # Prepare container images for docker compilers (adds cmake on top of
    # the base image); drop compilers whose image cannot be prepared.
    usable = []
    for spec in compilers:
        info = parse_compiler(spec)
        if info["docker"] and not ensure_docker_image(info):
            print(f"Skipping {spec}: could not prepare container image")
            continue
        usable.append(spec)
    if not usable:
        sys.exit("error: no usable compiler left")
    compilers = usable

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
    skipped = [c for c in (COMPILERS if args.all else [])
               if c not in {c for c, _, _, _ in results}]
    print(f"\nSummary: {len(results) - len(failed)} succeeded, "
          f"{len(failed)} failed"
          + (f", skipped: {', '.join(skipped)}" if skipped else ""))
    for compiler, name, log in failed:
        print(f"\n=== {compiler}/{name} ===\n{log}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
