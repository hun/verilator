// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL shifts with a negative count are the opposite shift
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

    // b = "1011". A negative count is the opposite shift (IEEE 1076-2008
    // 9.2.4). These are the values GHDL reports.
    int fails = 0;
    topp->b = 0b1011;
    struct Case final {
        int n;
        int y1;
        int y2;
        int y3;
        int y4;
    };
    static const Case cases[2]
        = {{1, 0b0110, 0b0101, 0b0111, 0b1101}, {-1, 0b0101, 0b0110, 0b1101, 0b0111}};
    for (const Case& c : cases) {
        topp->n = c.n;
        topp->eval();
        if (topp->y1 != c.y1) {
            std::printf("%%Error: n=%d y1=%d exp=%d\n", c.n, topp->y1, c.y1);
            ++fails;
        }
        if (topp->y2 != c.y2) {
            std::printf("%%Error: n=%d y2=%d exp=%d\n", c.n, topp->y2, c.y2);
            ++fails;
        }
        if (topp->y3 != c.y3) {
            std::printf("%%Error: n=%d y3=%d exp=%d\n", c.n, topp->y3, c.y3);
            ++fails;
        }
        if (topp->y4 != c.y4) {
            std::printf("%%Error: n=%d y4=%d exp=%d\n", c.n, topp->y4, c.y4);
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
