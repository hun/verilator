// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL if-generate and elsif-generate test
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
            for (int c = 0; c < 2; ++c) {
                topp->a = a;
                topp->b = b;
                topp->c = c;
                topp->eval();

                const int y = static_cast<int>(topp->y);
                const int z = static_cast<int>(topp->z);
                if (y != a) {
                    std::printf("%%Error: a=%d y=%d\n", a, y);
                    ++fails;
                }
                if (z != c) {
                    std::printf("%%Error: a=%d b=%d c=%d z=%d\n", a, b, c, z);
                    ++fails;
                }
                if (static_cast<int>(topp->w) != 1 || static_cast<int>(topp->v) != a) {
                    std::printf("%%Error: a=%d w=%d v=%d\n", a,
                                static_cast<int>(topp->w), static_cast<int>(topp->v));
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
