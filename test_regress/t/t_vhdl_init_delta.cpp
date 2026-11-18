// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a signal read in the time-zero activation keeps its old value
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
    // The time-zero activation: bb reads the OLD aa, so b_o is still 0
    topp->clk = 0;
    topp->eval();
    if (topp->b_o != 0) {
        std::printf("%%Error: time-zero b_o=%d exp=0\n", topp->b_o);
        ++fails;
    }
    // After the first clock edge aa has been 1 for a cycle, so bb catches up
    topp->clk = 1;
    topp->eval();
    if (topp->b_o != 1) {
        std::printf("%%Error: after edge b_o=%d exp=1\n", topp->b_o);
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
