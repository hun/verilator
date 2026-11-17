// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL conditional signal/variable assignment
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

    //          en  sel  a    b  -> o   cnt k   hold
    static const int inEn[6] = {1, 0, 1, 1, 0, 0};
    static const int inSel[6] = {1, 0, 0, 1, 1, 0};
    static const int inA[6] = {5, 1, 2, 12, 3, 4};
    static const int inB[6] = {9, 3, 7, 1, 6, 2};
    static const int expO[6] = {5, 3, 7, 12, 3, 2};
    static const int expCnt[6] = {5, 0, 2, 12, 0, 0};
    static const int expK[6] = {5, 5, 2, 12, 12, 12};
    static const int expHold[6] = {5, 5, 2, 12, 6, 6};

    int fails = 0;
    topp->clk = 0;
    topp->eval();
    for (int i = 0; i < 6; ++i) {
        topp->en = inEn[i];
        topp->sel = inSel[i];
        topp->a = inA[i];
        topp->b = inB[i];
        topp->eval();
        topp->clk = 1;
        topp->eval();
        topp->eval();
        const int o = topp->o;
        const int cnt = topp->cnt;
        const int k = topp->k;
        const int hold = topp->hold;
        if (o != expO[i] || cnt != expCnt[i] || k != expK[i] || hold != expHold[i]) {
            std::printf("%%Error: i=%d o=%d cnt=%d k=%d hold=%d exp o=%d cnt=%d k=%d hold=%d\n", i,
                        o, cnt, k, hold, expO[i], expCnt[i], expK[i], expHold[i]);
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
