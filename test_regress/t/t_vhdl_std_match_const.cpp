// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a don't-care in a constant is a wildcard
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

    // pat is "1-0": bit 1 is a wildcard. GHDL: 110 and 100 match, 111 and
    // 010 do not.
    int fails = 0;
    struct Case final {
        int x;
        int m1;
        int m2;
    };
    static const Case cases[4] = {{0b110, 1, 1}, {0b100, 1, 1}, {0b111, 0, 0}, {0b010, 0, 0}};
    for (const Case& c : cases) {
        topp->x = c.x;
        topp->eval();
        if (topp->m1 != c.m1) {
            std::printf("%%Error: x=%d m1=%d exp=%d\n", c.x, topp->m1, c.m1);
            ++fails;
        }
        if (topp->m2 != c.m2) {
            std::printf("%%Error: x=%d m2=%d exp=%d\n", c.x, topp->m2, c.m2);
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
