// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL interface view of an AXI-Stream record, all port forms
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static void tick(VM_PREFIX* topp) {
    topp->clk_i = 0;
    topp->eval();
    topp->clk_i = 1;
    topp->eval();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    topp->rst_i = 1;
    topp->sin_valid = 0;
    topp->sin_data = 0;
    topp->mout_ready = 1;
    tick(topp);
    tick(topp);
    topp->rst_i = 0;

    int fails = 0;
    const int stream[3] = {0xA5, 0x3C, 0xF0};
    int seen = 0;
    // Two pipeline stages: a beat sent at cycle n arrives at the output at cycle n + 2
    for (int cycle = 0; cycle < 8; ++cycle) {
        topp->sin_valid = cycle < 3;
        topp->sin_data = cycle < 3 ? stream[cycle] : 0;
        tick(topp);
        if (cycle >= 1 && topp->mout_valid) {
            const int beat = cycle - 1;
            if (beat < 3 && topp->mout_data != stream[beat]) {
                std::printf("%%Error: beat %d data=%x exp=%x\n", beat, topp->mout_data,
                            stream[beat]);
                ++fails;
            }
            ++seen;
        }
    }
    if (!topp->sin_ready) {
        std::printf("%%Error: sin_ready low with an idle pipe\n");
        ++fails;
    }
    if (seen < 3) {
        std::printf("%%Error: only %d valid beats seen\n", seen);
        ++fails;
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
