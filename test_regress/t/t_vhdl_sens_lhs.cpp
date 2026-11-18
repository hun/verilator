// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL process(all) is sensitive to a target index signal
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
    // d alone writes mem(0)
    topp->d = 1;
    topp->addr = 0;
    topp->eval();
    if (topp->q2 != 0) {
        std::printf("%%Error: addr=0 q2=%d exp=0\n", topp->q2);
        ++fails;
    }
    // An addr-only change must re-trigger process(all) and write mem(2)
    topp->addr = 2;
    topp->eval();
    if (topp->q2 != 1) {
        std::printf("%%Error: addr=2 q2=%d exp=1\n", topp->q2);
        ++fails;
    }
    topp->addr = 3;
    topp->eval();
    if (topp->q3 != 1) {
        std::printf("%%Error: addr=3 q3=%d exp=1\n", topp->q3);
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
