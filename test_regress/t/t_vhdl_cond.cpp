// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL conditional and selected assignment test
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
    for (int sel = 0; sel < 2; ++sel) {
        for (int a = 0; a < 2; ++a) {
            for (int b = 0; b < 2; ++b) {
                topp->sel = sel;
                topp->a = a;
                topp->b = b;
                topp->eval();

                const int y = static_cast<int>(topp->y);
                const int z = static_cast<int>(topp->z);
                const int expectY = sel ? a : b;
                const int expectZ = sel ? 3 : 0;
                if (y != expectY) {
                    std::printf("%%Error: sel=%d a=%d b=%d y=%d expected %d\n", sel, a, b, y,
                                expectY);
                    ++fails;
                }
                if (z != expectZ) {
                    std::printf("%%Error: sel=%d z=%d expected %d\n", sel, z, expectZ);
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
