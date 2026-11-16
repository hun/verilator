// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL inlined function writing a slice of a local
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

    const auto tick = [&]() {
        topp->clk = 0;
        topp->eval();
        topp->clk = 1;
        topp->eval();
    };

    topp->clk = 0;
    topp->rst = 1;
    tick();
    tick();
    topp->rst = 0;

    int fails = 0;
    // The low three bits count up, so the record element arrives as 1, 2, 3, 4, 5
    for (unsigned want = 1; want <= 5; ++want) {
        tick();
        if (topp->o != want) {
            std::printf("%%Error: o=%02x exp=%02x\n", (unsigned)topp->o, want);
            ++fails;
        }
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
