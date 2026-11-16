// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL discrete subtypes as ranges, std_match, variable slices
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    topp->a = 0xa5;
    topp->eval();

    int fails = 0;
    // a = 1010_0101: the high nibble matches "1010", the full word does not match
    if (topp->m0 != 1) {
        std::printf("%%Error: m0=%d exp=1\n", topp->m0);
        ++fails;
    }
    if (topp->m1 != 0) {
        std::printf("%%Error: m1=%d exp=0\n", topp->m1);
        ++fails;
    }
    // The four elements of the subtype-indexed array
    if (topp->w != 4) {
        std::printf("%%Error: w=%d exp=4\n", topp->w);
        ++fails;
    }
    // Low byte is a, high byte is its complement
    if (topp->r != 0x5aa5) {
        std::printf("%%Error: r=%04x exp=5aa5\n", topp->r);
        ++fails;
    }

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
