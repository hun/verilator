// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL choice-list test
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
    const int yExp[8] = {1, 1, 1, 2, 2, 3, 3, 3};
    const int wExp[4] = {1, 1, 2, 3};
    for (int idx = 0; idx < 8; ++idx) {
        for (int sel = 0; sel < 4; ++sel) {
            topp->idx = idx;
            topp->sel = sel;
            topp->eval();

            const int y = static_cast<int>(topp->y);
            const int w = static_cast<int>(topp->w);
            const int m = static_cast<int>(topp->m);
            const int q = static_cast<int>(topp->q);
            const int p = static_cast<int>(topp->p);
            if (y != yExp[idx]) {
                std::printf("%%Error: idx=%d y=%d\n", idx, y);
                ++fails;
            }
            if (w != wExp[sel]) {
                std::printf("%%Error: sel=%d w=%d\n", sel, w);
                ++fails;
            }
            if (m != 0x07) {
                std::printf("%%Error: idx=%d m=%d\n", idx, m);
                ++fails;
            }
            if (q != 0x04) {
                std::printf("%%Error: idx=%d q=%d\n", idx, q);
                ++fails;
            }
            if (p != 0x07) {
                std::printf("%%Error: idx=%d p=%d\n", idx, p);
                ++fails;
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
