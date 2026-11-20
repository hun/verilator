// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL dynamic slice test
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
    const int yExp[4] = {0x78, 0x56, 0x34, 0x12};
    const int zExp[4] = {0x0, 0x2, 0x3, 0x1};
    const int ayExp[4] = {0x12, 0x34, 0x56, 0x78};
    for (int i = 0; i < 4; ++i) {
        topp->i = i;
        topp->eval();
        if (static_cast<int>(topp->y) != yExp[i]) {
            std::printf("%%Error: i=%d y=%d\n", i, static_cast<int>(topp->y));
            ++fails;
        }
        if (static_cast<int>(topp->z) != zExp[i]) {
            std::printf("%%Error: i=%d z=%d\n", i, static_cast<int>(topp->z));
            ++fails;
        }
        if (static_cast<int>(topp->ay) != ayExp[i]) {
            std::printf("%%Error: i=%d ay=%d\n", i, static_cast<int>(topp->ay));
            ++fails;
        }
    }

    // Write the slice at i=2 on a clock edge, then read it back
    topp->i = 2;
    topp->din = 0xA5;
    topp->we = 1;
    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    topp->we = 0;
    topp->eval();
    if (static_cast<int>(topp->q) != 0xA5) {
        std::printf("%%Error: q=%d expected %d\n", static_cast<int>(topp->q), 0xA5);
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
