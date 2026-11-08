// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL synchronous reset test
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
    topp->rst = 0;
    topp->d = 0;
    topp->eval();

    // Load a value different from the reset value
    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    if (topp->q != 0) {
        std::printf("%%Error: load q=%d\n", static_cast<int>(topp->q));
        ++fails;
    }
    topp->clk = 0;
    topp->eval();

    // Asserting reset without a clock edge must not change the output
    topp->rst = 1;
    topp->eval();
    if (topp->q != 0) {
        std::printf("%%Error: reset without edge q=%d\n", static_cast<int>(topp->q));
        ++fails;
    }

    // The next clock edge applies the reset
    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    if (topp->q != 1) {
        std::printf("%%Error: reset edge q=%d\n", static_cast<int>(topp->q));
        ++fails;
    }
    topp->clk = 0;
    topp->eval();

    // Release and load again
    topp->rst = 0;
    topp->eval();
    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    if (topp->q != 0) {
        std::printf("%%Error: post q=%d\n", static_cast<int>(topp->q));
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
