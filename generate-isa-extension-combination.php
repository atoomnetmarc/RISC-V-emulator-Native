<?php

/**
 * Copyright 2023-2024 Marc Ketel
 * SPDX-License-Identifier: Apache-2.0
 *
 * Try to generate all the extension subset combinations for use in platformio_isa-extension-combination_env.ini to compile the emulator.
 *
 * Generate command: php generate-isa-extension-combination.php > platformio_isa-extension-combination_env.ini
 *
 * The list of combinations might be a bit much.
 */

/**
 * List of base ISA keys with the emulator defines.
 */
$baseIntegerISA = [
    'RV32I' => [],
    /*
    'RV32E' => [],
    'RV64I' => [],
    'RV64E' => [],
    'RV128I' => [],
    */
];

/**
 * List of subset keys with the emulator defines.
 *
 * The first define of each extension enables the extension, the following defines are dependencies of that extension.
 *
 * Sort subset the same as: https://gcc.gnu.org/onlinedocs/gcc/RISC-V-Options.html#index-march-14
 */
$subset = [
    'M' => [
        '-D RVE_E_M=1',
    ],
    'A' => [
        '-D RVE_E_A=1',
    ],
    /*
    'F' => [
        '-D RVE_E_F=1',
        '-D RVE_E_ZICSR=1',
    ],
    */
    /*
    'D' => [
        '-D RVE_E_D=1',
        '-D RVE_E_F=1',
        '-D RVE_E_ZICSR=1',
    ],
    */
    'C' => [
        '-D RVE_E_C=1',
    ],
    'Zicsr' => [
        '-D RVE_E_ZICSR=1',
    ],
    'Zifencei' => [
        '-D RVE_E_ZIFENCEI=1',
    ],
    'Zba' => [
        '-D RVE_E_ZBA=1',
    ],
    'Zbb' => [
        '-D RVE_E_ZBB=1',
    ],
    'Zbc' => [
        '-D RVE_E_ZBC=1',
    ],
    'Zbs' => [
        '-D RVE_E_ZBS=1',
    ],
];


$subsetKeyCombinations = generateKeyCombinations(array_keys($subset));

foreach ($baseIntegerISA as $biKey => $biValue) {
    foreach ($subsetKeyCombinations as $subsetKeyCombination) {
        $uniqueValues = getUniqueValuesFromCombination($subsetKeyCombination, $subset);

        $isa = getISAString($biKey, $subsetKeyCombination);

        // Skip illegal combinations like D without F.
        if (!legalCombination($subset, $subsetKeyCombination)) {
            continue;
        }

        print "[env:{$isa}]\n";
        print "extends           = common\n";
        print "build_flags       =\n";
        print "  \${common.build_flags}\n";
        foreach (array_merge($biValue, $uniqueValues) as $value) {
            print "  {$value}\n";
        }
        print "\n";
    }
}

/**
 * Generate all combinations of a key value of an array.
 */
function generateKeyCombinations($keys, $currentCombination = [])
{
    if (empty($keys)) {
        return [$currentCombination];
    }

    $key = array_shift($keys);
    $combinations = generateKeyCombinations($keys, $currentCombination);
    $newCombination = array_merge($currentCombination, [$key]);
    $combinations = array_merge($combinations, generateKeyCombinations($keys, $newCombination));

    return $combinations;
}

/**
 * Get unique values of all the specified keys.
 */
function getUniqueValuesFromCombination($combination, $subset)
{
    $uniqueValues = [];
    foreach ($combination as $key) {
        foreach ($subset[$key] as $value) {
            if (!in_array($value, $uniqueValues)) {
                $uniqueValues[] = $value;
            }
        }
    }

    return $uniqueValues;
}

/**
 * Generate a possibly valid ISA-string.
 */
function getISAString($baseISA, $subset)
{
    $isa = $baseISA;

    //Detect G extension.
    $gsubset = ['I', 'M', 'A', 'F', 'D', 'Zicsr', 'Zifencei'];
    $result = array_diff($gsubset, array_merge([substr($baseISA, -1)], $subset));
    if (count($result) === 0) {
        $subset[array_search('I', $subset)] = 'G';
        $subset = array_diff($subset, $gsubset);
    }

    $previousValue = '';

    foreach ($subset as $value) {
        if (strlen($previousValue) > 1) {
            $isa .= '_';
        }
        $isa .= $value;
        $previousValue = $value;
    }

    return $isa;
}

/**
 * Detect illegal combinations like D without F.
 */
function legalCombination($subset, $subsetKeyCombination)
{
    foreach($subsetKeyCombination as $subsetKey) {
        $subsetDependencies = $subset[$subsetKey];
        array_shift($subsetDependencies);

        // Test if dependencies can be found.
        foreach($subsetDependencies as $subsetDependency) {
            $dependencyFound = false;
            foreach(array_intersect_key($subset, array_flip($subsetKeyCombination)) as $s) {
                if ($s[0] == $subsetDependency) {
                    $dependencyFound = true;
                    break;
                }
            }

            if (!$dependencyFound) {
                return false;
            }
        }
    }

    return true;
}