// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: 2**n with a run-time exponent
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
    topp->clk = 0;
    int fails = 0;
    int count = 0;
    for (int cycle = 0; cycle < 60; ++cycle) {
        const bool rst = cycle == 0 || cycle == 40 || cycle == 41;
        topp->rst = rst ? 1 : 0;
        topp->eval();
        // 2**count for the count the previous edge left
        const int expected = 1 << count;
        if (topp->o != expected || topp->s != 65) {
            if (fails < 8) {
                std::printf("%%Error: cycle %d o=%d expected %d, s=%d\n", cycle,
                            static_cast<int>(topp->o), expected, static_cast<int>(topp->s));
            }
            ++fails;
        }
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
        if (rst)
            count = 0;
        else if (count != 20)
            ++count;
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
