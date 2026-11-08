// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL numeric_std conversion test
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
        const int z = static_cast<int>(topp->z);
        const int v = static_cast<int>(topp->v);
        const int w = static_cast<int>(topp->w);
        if (y != a) {
            std::printf("%%Error: a=%d y=%d\n", a, y);
            ++fails;
        }
        if (z != 0xf) {
            std::printf("%%Error: a=%d z=%d\n", a, z);
            ++fails;
        }
        if (v != 0xff) {
            std::printf("%%Error: a=%d v=%d\n", a, v);
            ++fails;
        }
        if (w != a) {
            std::printf("%%Error: a=%d w=%d\n", a, w);
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
