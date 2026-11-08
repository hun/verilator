// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL generic type parameters test
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
    topp->clk = 0;
    topp->eval();
    for (int sel = 0; sel < 2; ++sel) {
        for (int a4 = 0; a4 < 16; a4 += 5) {
            for (int b4 = 0; b4 < 16; b4 += 7) {
                for (int a2 = 0; a2 < 4; ++a2) {
                    for (int b2 = 0; b2 < 4; ++b2) {
                        topp->sel = sel;
                        topp->a4 = a4;
                        topp->b4 = b4;
                        topp->a2 = a2;
                        topp->b2 = b2;
                        topp->eval();

                        const int mux4 = sel == 0 ? a4 : b4;
                        const int mux2 = sel == 0 ? a2 : b2;
                        if (static_cast<int>(topp->y2) != mux2) {
                            std::printf("%%Error: sel=%d y2=%d expected %d\n", sel,
                                        static_cast<int>(topp->y2), mux2);
                            ++fails;
                        }
                        topp->clk = 1;
                        topp->eval();
                        if (static_cast<int>(topp->y4) != mux4) {
                            std::printf("%%Error: sel=%d y4=%d expected %d\n", sel,
                                        static_cast<int>(topp->y4), mux4);
                            ++fails;
                        }
                        topp->clk = 0;
                        topp->eval();
                    }
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
