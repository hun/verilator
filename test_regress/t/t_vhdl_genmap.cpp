// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL generic map specialization test
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
    topp->a = 0xa5;
    topp->b = 2;
    topp->c = 0xc;
    topp->d = 1;
    topp->eval();
    if (static_cast<int>(topp->y) != 0x5a) {
        std::printf("%%Error: y=%d\n", static_cast<int>(topp->y));
        ++fails;
    }
    if (static_cast<int>(topp->z) != 2) {
        std::printf("%%Error: z=%d\n", static_cast<int>(topp->z));
        ++fails;
    }
    if (static_cast<int>(topp->w) != 0xc) {
        std::printf("%%Error: w=%d\n", static_cast<int>(topp->w));
        ++fails;
    }
    if (static_cast<int>(topp->v) != 1) {
        std::printf("%%Error: v=%d\n", static_cast<int>(topp->v));
        ++fails;
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
