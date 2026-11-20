// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: C++ driver for the mixed SystemVerilog/VHDL design
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

    int fails = 0;
    unsigned exp = 0;
    topp->clk = 0;
    topp->en = 0;
    topp->eval();
    // Enabled on two of every three cycles, so the VHDL counter inside the SystemVerilog
    // top has to follow the count this driver keeps
    for (int cycle = 0; cycle < 40; ++cycle) {
        const bool en = (cycle % 3) != 2;
        topp->en = en;
        topp->clk = 1;
        topp->eval();
        if (en) ++exp;
        if (topp->q != (exp & 0xf)) {
            std::printf("%%Error: cycle %d q=%d expected %d\n", cycle, topp->q, exp & 0xf);
            ++fails;
        }
        topp->clk = 0;
        topp->eval();
    }

    if (fails) {
        std::printf("%%Error: %d mismatches\n", fails);
    } else {
        std::printf("*-* All Finished *-*\n");
    }
    delete topp;
    return fails ? 1 : 0;
}
