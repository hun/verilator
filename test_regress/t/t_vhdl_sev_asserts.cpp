// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: statements after consecutive severity-failure asserts still run
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
    const auto check = [&](int want) {
        topp->clk = 0;
        topp->eval();
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
        const int got = static_cast<int>(topp->o);
        if (got != want) {
            std::printf("%%Error: o=%d expected %d\n", got, want);
            ++fails;
        }
    };

    // The counter must advance; the second assert used to gate it low
    check(1);
    check(2);
    check(3);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
