// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: unsigned and signed operands under std_logic_arith
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
    const struct {
        unsigned u;
        unsigned s;  // Four bits, two's complement
        unsigned sum;
        unsigned diff;
        unsigned wide;
    } cases[] = {{9, 0xD, 6, 12, 6},  // 9 + -3, 9 - -3
                 {1, 0x8, 0x19, 9, 0x39},  // 1 + -8 = -7, 1 - -8
                 {15, 0xF, 14, 16, 14}};  // 15 + -1, 15 - -1
    for (const auto& c : cases) {
        topp->u = c.u;
        topp->s = c.s;
        topp->eval();
        if (topp->sum != c.sum || topp->diff != c.diff || topp->wide != c.wide) {
            std::printf("%%Error: u=%u s=%x sum=%x diff=%x wide=%x\n", c.u, c.s,
                        static_cast<unsigned>(topp->sum), static_cast<unsigned>(topp->diff),
                        static_cast<unsigned>(topp->wide));
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
