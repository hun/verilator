// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL mixed signed/unsigned and int/vector operands
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

    // vs = signed(v), vu = unsigned(v), INT = 5 ("101").
    // sge = (-vs) >= signed"110" (= -2), where the unary minus is a 3-bit signed negate,
    // so v=4 (-vs of -4) overflows back to -4. ueq = vu ?= 5; sne = vs ?/= 5.
    static const int expSge[8] = {1, 1, 1, 0, 0, 1, 1, 1};
    static const int expUeq[8] = {0, 0, 0, 0, 0, 1, 0, 0};
    static const int expSne[8] = {1, 1, 1, 1, 1, 0, 1, 1};

    int fails = 0;
    for (int v = 0; v < 8; ++v) {
        topp->v = v;
        topp->eval();
        const int sge = topp->sge, ueq = topp->ueq, sne = topp->sne;
        if (sge != expSge[v] || ueq != expUeq[v] || sne != expSne[v]) {
            std::printf("%%Error: v=%d sge=%d/%d ueq=%d/%d sne=%d/%d\n", v, sge, expSge[v], ueq,
                        expUeq[v], sne, expSne[v]);
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
