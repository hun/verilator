// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL `wait;` one-shot process test
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };

    topp->clk = 0;
    topp->eval();
    check("q after time zero", static_cast<int>(topp->q), 7);

    for (int cycle = 1; cycle <= 5; ++cycle) {
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
        check("q stays one-shot", static_cast<int>(topp->q), 7);
        check("r counts clock edges", static_cast<int>(topp->r), cycle);
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
