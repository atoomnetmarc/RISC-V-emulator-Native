#!/usr/bin/env python3
# Copyright Marc Ketel
# SPDX-License-Identifier: Apache-2.0
#
# Generate extension subset combinations for use in
# platformio_isa-extension-combination_env.ini to compile the emulator.
#
# Generate command:
#   python3 generate-isa-extension-combination.py > platformio_isa-extension-combination_env.ini
#
# By default a covering array of strength COVERING_STRENGTH is generated:
# every legal combination of COVERING_STRENGTH extensions appears together in
# at least one build, plus every extension alone. This is a mathematically
# minimal-ish proof that extensions work alone and in combination, without
# exhaustively building all 2^N combinations.
#
# Use --full to generate the exhaustive set of all legal combinations instead.
#
# Envs marked with "# smoke" are the smoke-test subset:
#  - default mode: every env (the array already is the fast set)
#  - --full mode:  every single-extension env plus every maximal legal
#                  combination (maximal under extension-set inclusion)

import argparse
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
# Sort extension and subsets the same as:
# https://gcc.gnu.org/onlinedocs/gcc/RISC-V-Options.html
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
            "-D RVE_E_ZBS=1",
        ],
    },
    "Zaamo": {"define": ["-D RVE_E_ZAAMO=1"]},
    "Zalrsc": {"define": ["-D RVE_E_ZALRSC=1"]},
    "Zmmul": {"define": ["-D RVE_E_ZMMUL=1"]},
    "Zicsr": {"define": ["-D RVE_E_ZICSR=1"]},
    "Zifencei": {"define": ["-D RVE_E_ZIFENCEI=1"]},
    "Zba": {"define": ["-D RVE_E_ZBA=1"]},
    "Zbb": {"define": ["-D RVE_E_ZBB=1"]},
    "Zbc": {"define": ["-D RVE_E_ZBC=1"]},
    "Zbs": {"define": ["-D RVE_E_ZBS=1"]},
}


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
    args = parser.parse_args()

    keys = list(SUBSET)
    all_legal = [
        comb for size in range(len(keys) + 1)
        for comb in itertools.combinations(keys, size)
        if legal_combination(SUBSET, list(comb))
    ]

    if args.full:
        combinations = all_legal
        # Smoke subset: singles plus every maximal legal combination.
        smoke = set(
            comb for comb in all_legal if len(comb) <= 1
        ) | set(maximal_combinations(all_legal))
    else:
        singles = [comb for comb in all_legal if len(comb) == 1]
        array = covering_array(all_legal, COVERING_STRENGTH)
        combinations = singles + array
        # In default mode the whole set is the smoke subset.
        smoke = set(combinations)

    for bi_key, bi_value in BASE_INTEGER_ISA.items():
        for subset_key_combination in combinations:
            unique_values = get_unique_values_from_combination(
                list(subset_key_combination), SUBSET
            )

            isa = get_isa_string(bi_key, list(subset_key_combination))

            print(f"[env:{isa}]")
            print(f"# act-config: rve-{isa.lower()}")
            if subset_key_combination in smoke:
                print("# smoke")
            for act_line in get_act_lines(list(subset_key_combination)):
                print(f"# {act_line}")
            print("extends           = common")
            print("build_flags       =")
            print("  ${common.build_flags}")
            for value in bi_value + unique_values:
                print(f"  {value}")
            print()


def maximal_combinations(combinations: list) -> list:
    """Return the combinations that are maximal under set inclusion."""
    maximal = []
    for comb in combinations:
        s = set(comb)
        if not any(s < set(other) for other in combinations):
            maximal.append(comb)
    return maximal


def covering_array(legal: list, strength: int) -> list:
    """
    Greedy randomized search for a covering array of the given strength over
    the legal combinations. Deterministic via SEED. Returns a list of
    combinations (tuples of subset keys) such that every legal
    strength-sized subcombination with every on/off pattern that occurs in
    some legal combination is covered by at least one row.
    """
    legal_sets = [frozenset(comb) for comb in legal]
    legal_by_size = {}
    for comb in legal:
        legal_by_size.setdefault(len(comb), []).append(comb)

    # Required coverage items: (subcombination, pattern) where the pattern
    # (which members of the subcombination are on) is realized by at least
    # one legal combination.
    required = set()
    for comb in legal:
        for sub in itertools.combinations(comb, strength):
            required.add((sub, frozenset(sub)))

    if not required:
        return []

    random.seed(SEED)
    best_rows = None
    for _ in range(GREEDY_TRIALS):
        rows = []
        remaining = set(required)
        while remaining:
            best_row, best_cov = None, -1
            for _ in range(GREEDY_CANDIDATES):
                size = random.randint(0, len(SUBSET))
                candidates = legal_by_size.get(size, [])
                if not candidates:
                    continue
                row = tuple(random.choice(candidates))
                row_set = frozenset(row)
                cov = sum(
                    1 for sub, pattern in remaining if row_set >= frozenset(sub)
                    and all(
                        (ext in row_set) == (ext in pattern) for ext in sub
                    )
                )
                if cov > best_cov:
                    best_cov, best_row = cov, row
            rows.append(best_row)
            row_set = frozenset(best_row)
            remaining = {
                (sub, pattern)
                for sub, pattern in remaining
                if not (
                    row_set >= frozenset(sub)
                    and all(
                        (ext in row_set) == (ext in pattern) for ext in sub
                    )
                )
            }
        if best_rows is None or len(rows) < len(best_rows):
            best_rows = rows

    # Deterministic output order.
    return sorted(set(best_rows))


def get_act_lines(combination: list) -> list:
    lines = []
    for key in combination:
        lines.extend(SUBSET[key].get("act", []))
    return lines


def get_unique_values_from_combination(combination: list, subset: dict) -> list:
    unique_values = []
    for key in combination:
        for value in subset[key]["define"]:
            if value not in unique_values:
                unique_values.append(value)

    return unique_values


def get_isa_string(base_isa: str, subset: list) -> str:
    isa = base_isa


    # Detect G extension.
    gsubset = ["I", "M", "A", "F", "D", "Zicsr", "Zifencei"]
    result = set(gsubset) - set([base_isa[-1]] + subset)
    if len(result) == 0:
        subset[subset.index("I")] = "G"
        subset = [value for value in subset if value not in gsubset]

    previous_value = ""

    for value in subset:
        if len(previous_value) > 1:
            isa += "_"
        isa += value
        previous_value = value

    return isa


def legal_combination(subset: dict, subset_key_combination: list) -> bool:
    for subset_key in subset_key_combination:
        subset_defines = subset[subset_key]["define"]
        # Skip the first define (the extension itself) - the rest are
        # dependencies
        subset_dependencies = subset_defines[1:]

        # Test if dependencies can be found.
        for subset_dependency in subset_dependencies:
            dependency_found = False
            for key in subset_key_combination:
                if key == subset_key:
                    continue  # skip current extension

                # Check if this extension provides the dependency
                if subset_dependency in subset[key]["define"]:
                    dependency_found = True
                    break

            if not dependency_found:
                return False

        # Check for excluded defines if this extension has them
        for excluded_define in subset[subset_key].get("exclude", []):
            for key in subset_key_combination:
                if key == subset_key:
                    continue  # skip current extension

                # Check if any other extension includes the excluded define
                if excluded_define in subset[key]["define"]:
                    return False

    return True


if __name__ == "__main__":
    main()
