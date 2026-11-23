// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL std_logic_unsigned arithmetic and comparison on std_logic_vector
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

    int errors = 0;
    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 8; ++y) {
            topp->a = x;
            topp->b = y;
            topp->eval();
            // The operands as numbers
            const int a = x;
            const int b = y;
            const int want[] = {(a + b) & 0xf,
                                (b - a) & 0xf,
                                (a * b) & 0x7f,
                                (a + 3) & 0xf,
                                (a - 5) & 0xf,
                                (a < b) | (a <= b) << 1 | (a > b) << 2 | (a >= b) << 3
                                    | (a == b) << 4 | (a != b) << 5 | (a > 2) << 6 | (1 < a) << 7};
            const int got[] = {topp->s5, topp->s6, topp->m, topp->i1, topp->i2, topp->r};
            for (int i = 0; i < 6; ++i) {
                if (got[i] != want[i]) {
                    std::printf("%%Error: a=%d b=%d output %d = 0x%x expected 0x%x\n", x, y, i,
                                got[i], want[i]);
                    ++errors;
                }
            }
        }
    }

    topp->final();
    delete topp;
    if (errors) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
