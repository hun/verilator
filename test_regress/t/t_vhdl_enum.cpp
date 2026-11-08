// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL enumeration types through GHDL XML test
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
    const int expect[3] = {1, 2, 0};  // run, done, idle

    topp->clk = 0;
    topp->eval();
    if (static_cast<int>(topp->state_o) != 0 || static_cast<int>(topp->q) != 1) {
        std::printf("%%Error: t=0 state=%d q=%d expected idle\n",
                    static_cast<int>(topp->state_o), static_cast<int>(topp->q));
        ++fails;
    }

    for (int i = 0; i < 3; ++i) {
        topp->clk = 1;
        topp->eval();
        const int state = static_cast<int>(topp->state_o);
        const int q = static_cast<int>(topp->q);
        if (state != expect[i]) {
            std::printf("%%Error: edge %d state=%d expected %d\n", i, state, expect[i]);
            ++fails;
        }
        if (q != (state == 0)) {
            std::printf("%%Error: edge %d q=%d state=%d\n", i, q, state);
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
