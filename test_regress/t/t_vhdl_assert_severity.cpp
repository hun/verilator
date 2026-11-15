// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL assert severity decides whether the run continues
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

    // Six clocks: the failing `severity warning` assertion must not stop the run
    for (int cycle = 1; cycle <= 6; ++cycle) {
        topp->clk = 0;
        topp->d = 1;
        topp->eval();
        topp->clk = 1;
        topp->eval();
    }

    int fails = 0;
    const auto check = [&fails](const char* name, int got, int expect) {
        if (got != expect) {
            std::printf("%%Error: %s=%d exp=%d\n", name, got, expect);
            ++fails;
        }
    };
    check("q", topp->q, 1);
    check("n", topp->n, 6);
    if (Verilated::gotFinish()) {
        std::printf("%%Error: a warning-severity assertion ended the run\n");
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
