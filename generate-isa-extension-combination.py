#!/usr/bin/env python3
# Copyright Marc Ketel
# SPDX-License-Identifier: Apache-2.0
#
# Generate all the extension subset combinations for use in
# platformio_isa-extension-combination_env.ini to compile the emulator.
#
# Generate command:
#   python3 generate-isa-extension-combination.py > platformio_isa-extension-combination_env.ini
#
# The list of combinations might be a bit much.

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
    "M": {"define": ["-D RVE_E_M=1"]},
    "A": {
        "define": ["-D RVE_E_A=1"],
        "act": ["act-exclude-extensions: Zalrsc"],
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
    "Zicsr": {"define": ["-D RVE_E_ZICSR=1"]},
    "Zifencei": {"define": ["-D RVE_E_ZIFENCEI=1"]},
    "Zba": {"define": ["-D RVE_E_ZBA=1"]},
    "Zbb": {"define": ["-D RVE_E_ZBB=1"]},
    "Zbc": {"define": ["-D RVE_E_ZBC=1"]},
    "Zbs": {"define": ["-D RVE_E_ZBS=1"]},
}


def main() -> None:
    subset_key_combinations = generate_key_combinations(list(SUBSET))

    for bi_key, bi_value in BASE_INTEGER_ISA.items():
        for subset_key_combination in subset_key_combinations:
            # Skip illegal combinations like D without F.
            if not legal_combination(SUBSET, subset_key_combination):
                continue

            unique_values = get_unique_values_from_combination(
                subset_key_combination, SUBSET
            )

            isa = get_isa_string(bi_key, list(subset_key_combination))

            print(f"[env:{isa}]")
            print(f"# act-config: rve-{isa.lower()}")
            for act_line in get_act_lines(subset_key_combination):
                print(f"# {act_line}")
            print("extends           = common")
            print("build_flags       =")
            print("  ${common.build_flags}")
            for value in bi_value + unique_values:
                print(f"  {value}")
            print()


def get_act_lines(combination: list) -> list:
    lines = []
    for key in combination:
        lines.extend(SUBSET[key].get("act", []))
    return lines


def generate_key_combinations(keys: list, current_combination: list = None) -> list:
    if current_combination is None:
        current_combination = []
    if not keys:
        return [current_combination]

    key = keys[0]
    combinations = generate_key_combinations(keys[1:], current_combination)
    new_combination = current_combination + [key]
    combinations += generate_key_combinations(keys[1:], new_combination)

    return combinations


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
