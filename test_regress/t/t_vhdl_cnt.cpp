// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL counter test
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
    // Reference: q is the previous value of the counter register.
    for (int i = 0; i < 16; ++i) {
        topp->clk = 1;
        topp->eval();
        const int q = static_cast<int>(topp->q);
        const int expect = i == 0 ? 0 : i - 1;
        if (q != expect) {
            std::printf("%%Error: edge %d: q=%d expected %d\n", i, q, expect);
            ++fails;
        }
        topp->clk = 0;
        topp->eval();
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
