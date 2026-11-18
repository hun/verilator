// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL predefined scalar attributes
//
// This file ONLY is placed in the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    topp->eval();
    if (topp->b1 != 1) {
        std::printf("%%Error: bit'high=%d exp=1\n", static_cast<int>(topp->b1));
        ++fails;
    }
    if (topp->b2 != 0) {
        std::printf("%%Error: bit'left=%d exp=0\n", static_cast<int>(topp->b2));
        ++fails;
    }
    if (topp->s1 != 0) {
        std::printf("%%Error: std_logic'left=%d exp=0\n", static_cast<int>(topp->s1));
        ++fails;
    }
    if (topp->n != 1) {
        std::printf("%%Error: std_logic'pos('1')=%d exp=1\n", topp->n);
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
