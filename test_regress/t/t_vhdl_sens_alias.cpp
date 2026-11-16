// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an alias of a signal in a sensitivity list and an edge call
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

    topp->clk = 0;
    topp->d = 0;
    topp->eval();

    // The register is clocked by the signal the alias denotes
    topp->d = 1;
    topp->clk = 1;
    topp->eval();
    const int afterOne = static_cast<int>(topp->q);
    topp->clk = 0;
    topp->eval();
    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    const int afterZero = static_cast<int>(topp->q);

    topp->final();
    delete topp;

    if (afterOne != 1 || afterZero != 0) {
        std::printf("%%Error: q=%d,%d expected 1,0\n", afterOne, afterZero);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}