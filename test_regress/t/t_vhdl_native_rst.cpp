// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL native frontend async reset test
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
    topp->clk = 0;
    topp->rst = 0;
    topp->eval();

    // The counter wraps after 3, so q lags the edge count modulo 4
    for (int i = 1; i <= 5; ++i) {
        topp->clk = 1;
        topp->eval();
        const int q = static_cast<int>(topp->q);
        if (q != (i & 3)) {
            std::printf("%%Error: edge %d: q=%d expected %d\n", i, q, i & 3);
            ++fails;
        }
        topp->clk = 0;
        topp->eval();
    }

    // Asynchronous reset takes effect without a clock edge
    topp->rst = 1;
    topp->eval();
    if (static_cast<int>(topp->q) != 0) {
        std::printf("%%Error: async reset: q=%d expected 0\n", static_cast<int>(topp->q));
        ++fails;
    }
    topp->rst = 0;
    topp->clk = 1;
    topp->eval();
    // The first edge after reset deassertion counts 0 -> 1
    if (static_cast<int>(topp->q) != 1) {
        std::printf("%%Error: after reset edge: q=%d expected 1\n",
                    static_cast<int>(topp->q));
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
