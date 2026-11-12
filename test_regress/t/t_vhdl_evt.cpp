// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL 'event on a signal element
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

    // {clk, d, expected q}: q updates only on a rising edge of clk(0). The
    // (3,0,1) step toggles bit 1 while bit 0 is high with d=0: only a correct
    // element event leaves q at 1 (a whole-vector event would latch d=0).
    static const int clkV[7] = {0, 1, 3, 2, 1, 0, 1};
    static const int dV[7] = {1, 1, 0, 1, 1, 0, 0};
    static const int qExp[7] = {0, 1, 1, 1, 1, 1, 0};

    int fails = 0;
    for (int i = 0; i < 7; ++i) {
        topp->clk = clkV[i];
        topp->d = dV[i];
        topp->eval();
        const int q = topp->q;
        if (q != qExp[i]) {
            std::printf("%%Error: step=%d clk=%d d=%d q=%d exp=%d\n", i, clkV[i], dV[i], q,
                        qExp[i]);
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
