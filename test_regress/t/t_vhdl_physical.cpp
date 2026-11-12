// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL physical (time) literals and the time type
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
    topp->eval();

    int fails = 0;
    const int got[4] = {static_cast<int>(topp->q_ratio), static_cast<int>(topp->q_half),
                        static_cast<int>(topp->q_big), static_cast<int>(topp->q_frac)};
    const int exp[4] = {25, 30, 1000, 333};
    const char* const name[4]
        = {"q_ratio", "q_half", "q_big", "q_frac"};  // 200ns/8ns, 60ns/2/1ns, 1s/1ms, us/ns
    for (int i = 0; i < 4; ++i) {
        if (got[i] != exp[i]) {
            std::printf("%%Error: %s=%d exp=%d\n", name[i], got[i], exp[i]);
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
