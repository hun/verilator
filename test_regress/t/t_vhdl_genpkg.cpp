// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL generic-package instantiation test
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

    int fails = 0;
    // inst5 binds add => 5 and max => 100: o1 = i + 5, o2 = 100 + 5 = 105.
    const int inputs[3] = {3, 40, 0};
    for (const int in : inputs) {
        topp->i = in;
        topp->eval();
        topp->eval();  // Let the combinational outputs settle
        const int o1 = static_cast<int>(topp->o1);
        const int o2 = static_cast<int>(topp->o2);
        if (o1 != in + 5) {
            std::printf("%%Error: i=%d o1=%d expected %d\n", in, o1, in + 5);
            ++fails;
        }
        if (o2 != 105) {
            std::printf("%%Error: i=%d o2=%d expected 105\n", in, o2);
            ++fails;
        }
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
