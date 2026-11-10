// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL is_x test
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
        for (int b = 0; b < 16; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();
            if (static_cast<int>(topp->x) != 0 || static_cast<int>(topp->y) != 0) {
                std::printf("%%Error: a=%d b=%d x=%d y=%d\n", a, b, static_cast<int>(topp->x),
                            static_cast<int>(topp->y));
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
