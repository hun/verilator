// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a bare wait suspends the process for good
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
    // Six rising edges, but the process suspends for good after the third
    topp->clk = 0;
    topp->eval();
    for (int edge = 0; edge < 6; ++edge) {
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
    }
    if (topp->cnt_o != 3) {
        std::printf("%%Error: cnt_o=%d exp=3\n", topp->cnt_o);
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
