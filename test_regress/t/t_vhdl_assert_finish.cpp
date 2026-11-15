// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL severity failure ends the run
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

    // One clock: the assertion fires and the run must end there
    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();

    int fails = 0;
    if (!Verilated::gotFinish()) {
        std::printf("%%Error: a failure-severity assertion did not end the run\n");
        ++fails;
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
