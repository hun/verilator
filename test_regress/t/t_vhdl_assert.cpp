// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL assert/report test
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
    topp->eval();
    for (int a = 0; a <= 5; ++a) {
        for (int din = 0; din < 2; ++din) {
            topp->a = a;
            topp->din = din;
            topp->eval();
            const int v = a > 3 ? 3 : a;
            if (static_cast<int>(topp->v) != v) {
                std::printf("%%Error: a=%d v=%d expected %d\n", a, static_cast<int>(topp->v), v);
                ++fails;
            }
            topp->clk = 1;
            topp->eval();
            if (static_cast<int>(topp->y) != din) {
                std::printf("%%Error: din=%d y=%d\n", din, static_cast<int>(topp->y));
                ++fails;
            }
            topp->clk = 0;
            topp->eval();
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
