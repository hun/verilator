// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL ports with an omitted mode test
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
    for (int i = 0; i < 4; ++i) {
        topp->a = i & 1;
        topp->b = (i >> 1) & 1;
        topp->eval();
        const int expect = topp->a & topp->b;
        if (topp->y != expect || topp->y2 != expect) {
            std::printf("%%Error: a=%d b=%d: y=%d y2=%d expected %d\n", topp->a, topp->b,
                        static_cast<int>(topp->y), static_cast<int>(topp->y2), expect);
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
