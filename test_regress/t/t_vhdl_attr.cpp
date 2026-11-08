// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL static attributes test
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
        topp->a = a;
        topp->eval();

        const int y = static_cast<int>(topp->y);
        const int n = static_cast<int>(topp->n);
        const int h = static_cast<int>(topp->h);
        const int asc = static_cast<int>(topp->asc);
        if (y != a) {
            std::printf("%%Error: a=%d y=%d\n", a, y);
            ++fails;
        }
        if (n != 4) {
            std::printf("%%Error: a=%d n=%d\n", a, n);
            ++fails;
        }
        if (h != 3) {
            std::printf("%%Error: a=%d h=%d\n", a, h);
            ++fails;
        }
        if (asc != 0) {
            std::printf("%%Error: a=%d asc=%d\n", a, asc);
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
