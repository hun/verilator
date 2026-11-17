// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL context declaration imports its packages
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
    const int got = static_cast<int>(topp->o);
    const int want = 9;
    if (got != want) {
        std::printf("%%Error: o=%d expected %d\n", got, want);
        topp->final();
        delete topp;
        return 1;
    }

    topp->final();
    delete topp;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
