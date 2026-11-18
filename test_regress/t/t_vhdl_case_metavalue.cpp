// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a case over std_logic literals that the two-state model merges
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
    for (unsigned value = 0; value < 2; ++value) {
        topp->a = value;
        topp->v = 0xa;  // 1010
        topp->eval();
        if (static_cast<unsigned>(topp->o) != value) {
            std::printf("%%Error: a=%u o=%u\n", value, static_cast<unsigned>(topp->o));
            ++fails;
        }
        if (static_cast<unsigned>(topp->w) != 0xa) {
            std::printf("%%Error: v=1010 w=%x\n", static_cast<unsigned>(topp->w));
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
