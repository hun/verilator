// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL clk'event clock idiom test
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
    topp->clk = 0;
    topp->d = 0;
    topp->eval();

    topp->d = 1;
    topp->clk = 1;
    topp->eval();
    if (topp->q != 1) {
        std::printf("%%Error: load1 q=%d\n", static_cast<int>(topp->q));
        ++fails;
    }
    topp->clk = 0;
    topp->eval();

    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    if (topp->q != 0) {
        std::printf("%%Error: load0 q=%d\n", static_cast<int>(topp->q));
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
