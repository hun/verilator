// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL process(all) is sensitive to an entity-declared signal
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
    // The s event re-triggers the process, so y catches up within the cycle
    topp->a = 1;
    topp->eval();
    if (topp->y != 1) {
        std::printf("%%Error: a=1 y=%d exp=1\n", topp->y);
        ++fails;
    }
    topp->a = 0;
    topp->eval();
    if (topp->y != 0) {
        std::printf("%%Error: a=0 y=%d exp=0\n", topp->y);
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
