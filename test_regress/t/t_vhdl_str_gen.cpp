// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL string-generic generate condition
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

    topp->eval();

    int fails = 0;
    // i1 binds val => "one" so the '=' branch drives o1 = 1
    if (topp->o1 != 1) {
        std::printf("%%Error: o1=%d exp=1\n", topp->o1);
        ++fails;
    }
    // i2 binds val => "two" so the '/=' branch drives o2 = 0
    if (topp->o2 != 0) {
        std::printf("%%Error: o2=%d exp=0\n", topp->o2);
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
