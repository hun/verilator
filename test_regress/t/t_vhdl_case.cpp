// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL case, variables, indexing and slicing test
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
        for (int sel = 0; sel < 4; ++sel) {
            topp->a = a;
            topp->sel = sel;
            topp->eval();

            const int y = static_cast<int>(topp->y);
            const int z = static_cast<int>(topp->z);
            const int w = static_cast<int>(topp->w);
            int expectY = 0;
            if (sel == 0) {
                expectY = a & 1;
            } else if (sel == 1) {
                expectY = (a >> 3) & 1;
            } else {
                expectY = 1;
            }
            if (z != (a & 3)) {
                std::printf("%%Error: a=%d sel=%d z=%d\n", a, sel, z);
                ++fails;
            }
            if (w != ((a >> 2) & 3)) {
                std::printf("%%Error: a=%d sel=%d w=%d\n", a, sel, w);
                ++fails;
            }
            if (y != expectY) {
                std::printf("%%Error: a=%d sel=%d y=%d expected %d\n", a, sel, y, expectY);
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
