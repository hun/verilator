// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL array of unconstrained vectors, constrained at use
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
        const int d = topp->d;
        const int b = topp->b;
        const int e = topp->e;
        // row 1 holds a(3 downto 0); whole-element m(0) is a(7 downto 4)
        const int expO = a & 0xF;
        const int expD = (a >> 4) & 0xF;
        const int expB = (a >> 4) & 1;  // m(0)(0) is the low bit of row 0 == a(4)
        const int expE = (a >> 2) & 1;  // mm(1)(1)(2) is bit 2 of row (3 downto 0) == a(2)
        if (o != expO || d != expD || b != expB || e != expE) {
            std::printf("%%Error: a=%02x o=%x d=%x b=%x e=%x exp o=%x d=%x b=%x e=%x\n", a, o, d,
                        b, e, expO, expD, expB, expE);
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
