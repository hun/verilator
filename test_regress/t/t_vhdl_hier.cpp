// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL hierarchy test
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
    for (int a = 0; a < 2; ++a) {
        for (int b = 0; b < 2; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();
            const int y = static_cast<int>(topp->y);
            const int y2 = static_cast<int>(topp->y2);
            if (y != (a & b)) {
                std::printf("%%Error: a=%d b=%d y=%d\n", a, b, y);
                ++fails;
            }
            if (y2 != (a | b)) {
                std::printf("%%Error: a=%d b=%d y2=%d\n", a, b, y2);
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
