// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: dynamic VHDL enumeration succ bound check
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
    topp->e = 0;  // idle -> run
    topp->eval();
    if (static_cast<int>(topp->s) != 1) {
        std::printf("%%Error: succ(idle)=%d expected 1\n", static_cast<int>(topp->s));
        ++fails;
    }

    topp->e = 2;  // stop has no successor: this is a bound-check failure
    topp->eval();
    if (!Verilated::gotFinish()) {
        std::printf("%%Error: succ(stop) did not report a bound-check failure\n");
        ++fails;
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
