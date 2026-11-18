// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a loop range is evaluated once, at loop entry
//
// This file ONLY is placed in the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    // The range is fixed at entry, so the body assigning the bound variable
    // does not change the trip count. GHDL: 3 -> 4, 0 -> 1, 1 -> 2.
    int fails = 0;
    struct Case final {
        int nIn;
        int c;
    };
    static const Case cases[3] = {{3, 4}, {0, 1}, {1, 2}};
    for (const Case& c : cases) {
        topp->n_in = c.nIn;
        topp->eval();
        if (topp->c_o != c.c) {
            std::printf("%%Error: n_in=%d c_o=%d exp=%d\n", c.nIn, topp->c_o, c.c);
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
