// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL for loop, dynamic index and signed slice test
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
    for (int a = 0; a < 16; ++a) {
        for (int idx = 0; idx < 4; ++idx) {
            for (int s = 0; s < 16; ++s) {
                topp->a = a;
                topp->idx = idx;
                topp->s = s;
                topp->eval();

                const int y = static_cast<int>(topp->y);
                const int z = static_cast<int>(topp->z);
                const int u = static_cast<int>(topp->u);
                const int expectY = (a >> idx) & 1;
                const int expectU = (s & 8) ? (0xf0 | s) : s;
                if (y != expectY) {
                    std::printf("%%Error: a=%d idx=%d y=%d expected %d\n", a, idx, y, expectY);
                    ++fails;
                }
                if (z != a) {
                    std::printf("%%Error: a=%d z=%d\n", a, z);
                    ++fails;
                }
                if (u != expectU) {
                    std::printf("%%Error: s=%d u=%d expected %d\n", s, u, expectU);
                    ++fails;
                }
            }
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
