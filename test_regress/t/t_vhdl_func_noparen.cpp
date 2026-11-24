// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL function called without parentheses test
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

    topp->clk = 0;
    topp->rst = 0;
    topp->eval();
    int fails = 0;
    if (topp->q != 0) {
        std::printf("%%Error: q=%d before reset, expected 0\n", static_cast<int>(topp->q));
        ++fails;
    }
    topp->rst = 1;
    topp->clk = 1;
    topp->eval();
    if (topp->q != 0x0a) {
        std::printf("%%Error: q=%d after reset, expected 10\n", static_cast<int>(topp->q));
        ++fails;
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
