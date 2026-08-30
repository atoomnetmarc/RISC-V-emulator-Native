#!/usr/bin/env python3
# Copyright Marc Ketel
# SPDX-License-Identifier: Apache-2.0
#
# Generate extension subset combinations. Select the output format(s):
#   --ini PATH    PlatformIO env file ("-" for stdout), e.g.
#                 python3 generate-isa-extension-combination.py \
#                   --ini platformio_isa-extension-combination_env.ini
#   --cmake PATH  CMake combinations list for run-matrix.py, e.g.
#                 python3 generate-isa-extension-combination.py --cmake isa-combinations.cmake
# At least one of --ini/--cmake is required; both may be given.
#
# Default: covering array of strength COVERING_STRENGTH (every legal
# strength-sized combination co-occurs in some build, plus every extension
# and the base ISA alone). --full: all legal combinations.
# "# smoke" envs: default mode = all; --full mode = singles + maximal combos.

import argparse
import contextlib
import itertools
import random

COVERING_STRENGTH = 4
GREEDY_TRIALS = 30
GREEDY_CANDIDATES = 200
SEED = 0

BASE_INTEGER_ISA = {
    "RV32I": [],
    # "RV32E": [],
    # "RV64I": [],
    # "RV64E": [],
    # "RV128I": [],
}

# List of subset keys with the emulator defines.
#
# The first define of each extension enables the extension, the following
# defines are dependencies of that extension.
#
# "act" holds metadata for the RISC-V-emulator-ACT test wrapper. It is written
# into the .ini as a comment line above each env that includes the subset.
#
# Order matters twice:
#  - extensions and subsets are sorted the same as
#    https://gcc.gnu.org/onlinedocs/gcc/RISC-V-Options.html
#  - providers (C, Zicsr, ...) precede their dependents (Zcb, Zicntr, ...),
#    so every prefix of a legal combination is legal itself. The exhaustive
#    search in full_mode() relies on this.
SUBSET = {
    "M": {
        "define": ["-D RVE_E_M=1"],
        "exclude": [
            "-D RVE_E_ZMMUL=1",
        ],
    },
    "A": {
        "define": ["-D RVE_E_A=1"],
        "exclude": [
            "-D RVE_E_ZAAMO=1",
            "-D RVE_E_ZALRSC=1",
        ],
    },
    # "F": {"define": ["-D RVE_E_F=1", "-D RVE_E_ZICSR=1"]},
    # "D": {"define": ["-D RVE_E_D=1", "-D RVE_E_F=1", "-D RVE_E_ZICSR=1"]},
    "C": {"define": ["-D RVE_E_C=1"]},
    "B": {
        "define": ["-D RVE_E_B=1"],
        "exclude": [
            "-D RVE_E_ZBA=1",
            "-D RVE_E_ZBB=1",
            "-D RVE_E_ZBC=1",
            "-D RVE_E_ZBKC=1",
            "-D RVE_E_ZBS=1",
            "-D RVE_E_ZBKB=1",
            "-D RVE_E_ZBKX=1",
        ],
    },
    "Zaamo": {"define": ["-D RVE_E_ZAAMO=1"]},
    "Zalrsc": {"define": ["-D RVE_E_ZALRSC=1"]},
    "Zmmul": {"define": ["-D RVE_E_ZMMUL=1"]},
    "Zicsr": {"define": ["-D RVE_E_ZICSR=1"]},
    "Zifencei": {"define": ["-D RVE_E_ZIFENCEI=1"]},
    "Zba": {"define": ["-D RVE_E_ZBA=1"]},
    "Zbb": {"define": ["-D RVE_E_ZBB=1"]},
    "Zbc": {
        "define": ["-D RVE_E_ZBC=1"],
        "exclude": [
            "-D RVE_E_ZBKC=1",
        ],
    },
    "Zbkc": {"define": ["-D RVE_E_ZBKC=1"]},
    "Zbkb": {"define": ["-D RVE_E_ZBKB=1"]},
    "Zbkx": {"define": ["-D RVE_E_ZBKX=1"]},
    "Zbs": {"define": ["-D RVE_E_ZBS=1"]},
    "Zcb": {
        "define": ["-D RVE_E_ZCB=1", "-D RVE_E_C=1"],
    },
    "Zcmop": {
        "define": ["-D RVE_E_ZCMOP=1", "-D RVE_E_C=1"],
    },
    "Zicntr": {
        "define": ["-D RVE_E_ZICNTR=1", "-D RVE_E_ZICSR=1"],
    },
    "Zicond": {"define": ["-D RVE_E_ZICOND=1"]},
    "Zihintntl": {"define": ["-D RVE_E_ZIHINTNTL=1"]},
    "Zihintpause": {"define": ["-D RVE_E_ZIHINTPAUSE=1"]},
    "Zimop": {"define": ["-D RVE_E_ZIMOP=1"]},
    "Misalign": {"define": ["-D RVE_E_MISALIGNED=1"]},
}

KEYS = list(SUBSET)


def is_legal(combination: list) -> bool:
    """A combination is legal when every dependency of every extension is
    provided by another extension in the combination, and no extension
    includes a define that another extension excludes."""
    providers = {}  # define -> set of extensions providing it
    for key in combination:
        for define in SUBSET[key]["define"]:
            providers.setdefault(define, set()).add(key)

    for key in combination:
        ext = SUBSET[key]
        if any(not providers[dep] - {key} for dep in ext["define"][1:]):
            return False
        if any(providers.get(excl, set()) - {key}
               for excl in ext.get("exclude", [])):
            return False
    return True


# define-sets maintained incrementally while growing a combination
EXCLUDED_BY = {key: set(SUBSET[key].get("exclude", ())) for key in KEYS}


def can_add(key: str, defines: set, excluded: set) -> bool:
    """Whether key can be legally added to a combination with the given
    union of defines and union of excluded defines of its members."""
    ext = SUBSET[key]
    return (
        all(dep in defines for dep in ext["define"][1:])
        and not any(define in excluded for define in ext["define"])
        and not any(excl in defines for excl in ext.get("exclude", ()))
    )


def minimal_combination(key: str) -> tuple:
    """Return the smallest legal combination that contains the key."""
    combo = [key]
    for dependency in SUBSET[key]["define"][1:]:
        for provider in KEYS:
            if (
                SUBSET[provider]["define"][0] == dependency
                and provider not in combo
            ):
                # Single-letter extensions precede the multi-letter Z
                # extensions in the ISA string.
                combo.insert(0 if len(provider) == 1 else len(combo), provider)
    return tuple(combo)


def full_mode():
    """Yield (combination, is_maximal) for every legal combination.

    Depth-first search over KEYS, extending only with legal combinations.
    Because providers precede their dependents in KEYS, a key skipped earlier
    can never become addable later, so a combination is maximal exactly when
    no key after it can be legally added."""
    def emit(comb: list, defines: set, excluded: set, start: int):
        maximal = True
        for i in range(start, len(KEYS)):
            key = KEYS[i]
            if can_add(key, defines, excluded):
                maximal = False
                yield from emit(
                    comb + [key],
                    defines | set(SUBSET[key]["define"]),
                    excluded | EXCLUDED_BY[key],
                    i + 1,
                )
        yield tuple(comb), maximal

    yield from emit([], set(), set(), 0)


def random_row(required: list) -> tuple:
    """Grow a random legal combination from a random required subcombination."""
    row = list(random.choice(required))
    defines = {d for key in row for d in SUBSET[key]["define"]}
    excluded = set().union(*(EXCLUDED_BY[key] for key in row))
    for key in KEYS:
        if key not in row and random.random() < 0.5 and can_add(key, defines, excluded):
            row.append(key)
            defines |= set(SUBSET[key]["define"])
            excluded |= EXCLUDED_BY[key]
    return tuple(row)


def covering_array(required: list, strength: int) -> list:
    """Greedy randomized search (deterministic via SEED) for a small set of
    rows such that every required strength-sized subcombination is contained
    in at least one row."""
    required_sets = [frozenset(comb) for comb in required]
    if not required_sets:
        return []

    random.seed(SEED)
    best_rows = None
    for _ in range(GREEDY_TRIALS):
        rows, remaining = [], set(required_sets)
        while remaining:
            candidates = [random_row(required) for _ in range(GREEDY_CANDIDATES)]
            best_row = max(
                candidates,
                key=lambda row: sum(
                    map(frozenset(row).issuperset, remaining)
                ),
            )
            rows.append(best_row)
            row_set = frozenset(best_row)
            remaining = {sub for sub in remaining if not row_set >= sub}
        if best_rows is None or len(rows) < len(best_rows):
            best_rows = rows

    return sorted(set(best_rows))


def default_mode() -> list:
    """Return the covering-array combinations: the base ISA alone, every
    minimal single-extension combination, and the covering array rows."""
    singles = [()] + [minimal_combination(key) for key in KEYS]
    required = [
        comb for comb in itertools.combinations(KEYS, COVERING_STRENGTH)
        if is_legal(list(comb))
    ]
    return singles + [comb for comb in covering_array(required, COVERING_STRENGTH)
                      if comb not in singles]


def get_isa_string(base_isa: str, combination: list) -> str:
    subset = list(combination)

    # Detect G extension.
    gsubset = ["I", "M", "A", "F", "D", "Zicsr", "Zifencei"]
    if not set(gsubset) - {base_isa[-1], *subset}:
        subset[subset.index("I")] = "G"
        subset = [value for value in subset if value not in gsubset]

    isa = base_isa
    for i, value in enumerate(subset):
        if i and len(subset[i - 1]) > 1:
            isa += "_"
        isa += value
    return isa


def collect_env(bi_key: str, bi_value: list, combination: tuple, smoke: bool) -> dict:
    """Return a dict describing one build environment/combination."""
    unique_values = []
    for key in combination:
        for value in SUBSET[key]["define"]:
            if value not in unique_values:
                unique_values.append(value)

    isa = get_isa_string(bi_key, list(combination))
    return {
        "isa": isa,
        "smoke": smoke,
        "act": [line for key in combination for line in SUBSET[key].get("act", [])],
        "defines": bi_value + unique_values,
    }


def print_env(env: dict):
    print(f"[env:{env['isa']}]")
    print(f"# act-config: rve-{env['isa'].lower()}")
    if env["smoke"]:
        print("# smoke")
    for act_line in env["act"]:
        print(f"# {act_line}")
    print("extends           = common")
    print("build_flags       =")
    print("  ${common.build_flags}")
    for value in env["defines"]:
        print(f"  {value}")
    print()


def write_cmake(envs: list, path: str):
    """Write the combinations as a CMake list consumed by run-matrix.py.

    Each entry is "NAME;-DDEFINE=1;..." so the defines can be passed to
    cmake verbatim."""
    lines = [
        "# Generated by generate-isa-extension-combination.py -- do not edit.",
        "# Each entry: <name>;<-D define>... (defines passed to cmake verbatim)",
        "set(RVE_COMBINATIONS",
    ]
    for env in envs:
        defines = [value.replace("-D ", "-D") for value in env["defines"]]
        lines.append("  \"" + ";".join([env["isa"], *defines]) + "\"")
    lines.append(")")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate ISA extension combination envs."
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="generate the exhaustive set of all legal combinations "
        "instead of a covering array",
    )
    parser.add_argument(
        "--ini",
        metavar="PATH",
        help='write a PlatformIO env file to PATH ("-" for stdout)',
    )
    parser.add_argument(
        "--cmake",
        metavar="PATH",
        help="write a CMake combinations file (for run-matrix.py) to PATH",
    )
    args = parser.parse_args()

    if not args.ini and not args.cmake:
        parser.error("select at least one output: --ini PATH and/or --cmake PATH")

    envs = []
    for bi_key, bi_value in BASE_INTEGER_ISA.items():
        if args.full:
            for comb, maximal in full_mode():
                # Smoke subset: singles plus every maximal combination.
                envs.append(collect_env(bi_key, bi_value, comb, len(comb) <= 1 or maximal))
        else:
            # In default mode the whole set is the smoke subset.
            for comb in default_mode():
                envs.append(collect_env(bi_key, bi_value, comb, True))

    if args.ini:
        if args.ini == "-":
            for env in envs:
                print_env(env)
        else:
            with open(args.ini, "w") as f, contextlib.redirect_stdout(f):
                for env in envs:
                    print_env(env)
    if args.cmake:
        write_cmake(envs, args.cmake)


if __name__ == "__main__":
    main()
