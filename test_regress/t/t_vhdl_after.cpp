// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a signal assignment with a zero delay
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
    topp->d = 1;
    topp->eval();
    // One rising edge drives both the concurrent and the clocked assignment
    topp->clk = 1;
    topp->eval();
    const auto check = [&fails](const char* name, int got, int expect) {
        if (got != expect) {
            std::printf("%%Error: %s=%d exp=%d\n", name, got, expect);
            ++fails;
        }
    };
    check("q", topp->q, 1);
    check("r", topp->r, 1);

    topp->clk = 0;
    topp->d = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    check("q", topp->q, 0);
    check("r", topp->r, 0);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
