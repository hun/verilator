// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL qualified aggregate test
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
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 4; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();

            // unsigned'(0 => b(0)) is added to a
            const int yExp = (a + (b & 1)) & 0xF;
            // unsigned'(1 => a(1), 0 => a(0)) is ascending: index 0 is the msb
            const int zExp = (((a & 1) << 1) | ((a >> 1) & 1)) & 0x3;
            // unsigned'(1 to 2 => '1') is the two-bit value 3
            const int wExp = 3;

            const int y = static_cast<int>(topp->y);
            const int z = static_cast<int>(topp->z);
            const int w = static_cast<int>(topp->w);
            if (y != yExp) {
                std::printf("%%Error: a=%d b=%d y=%d expected %d\n", a, b, y, yExp);
                ++fails;
            }
            if (z != zExp) {
                std::printf("%%Error: a=%d z=%d expected %d\n", a, z, zExp);
                ++fails;
            }
            if (w != wExp) {
                std::printf("%%Error: a=%d w=%d expected %d\n", a, w, wExp);
                ++fails;
            }
            // Non-aggregate qualification operands evaluate to their value
            if (static_cast<int>(topp->qc) != 1) {
                std::printf("%%Error: qc=%d expected 1\n", static_cast<int>(topp->qc));
                ++fails;
            }
            if (static_cast<int>(topp->qn) != 0xA) {
                std::printf("%%Error: qn=%x expected a\n", static_cast<int>(topp->qn));
                ++fails;
            }
            if (static_cast<int>(topp->qe) != 3) {
                std::printf("%%Error: qe=%d expected 3\n", static_cast<int>(topp->qe));
                ++fails;
            }
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
