// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL two-dimensional array index constraint
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

    static const int inA[6] = {0x00, 0x0F, 0xF0, 0x5A, 0xA5, 0xFF};

    int fails = 0;
    for (const int a : inA) {
        topp->a = a;
        topp->eval();
        const int o = topp->o;
        const int b = topp->b;
        const int c = topp->c;
        const int expO = a & 0xF;  // row 1 holds a(3 downto 0)
        const int expB = (a >> 5) & 1;  // m(0, 1) is row 0's index-1 bit, i.e. a(5)
        if (o != expO || b != expB || c != (a & 1)) {
            std::printf("%%Error: a=%02x o=%x b=%x c=%x exp o=%x b=%x c=%x\n", a, o, b, c, expO,
                        expB, a & 1);
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
