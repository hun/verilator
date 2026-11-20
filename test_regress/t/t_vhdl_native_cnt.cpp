// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL native frontend counter test
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    // Initialize the clock edge state before the first rising edge.
    topp->clk = 0;
    topp->eval();

    int fails = 0;
    // q is the value of the counter register before the increment of this edge.
    for (int i = 0; i < 16; ++i) {
        topp->clk = 1;
        topp->eval();
        const int q = static_cast<int>(topp->q);
        if (q != (i & 0xf)) {
            std::printf("%%Error: edge %d: q=%d expected %d\n", i, q, i & 0xf);
            ++fails;
        }
        topp->clk = 0;
        topp->eval();
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
