// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL type declared from the subtype of an object
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
    for (const unsigned x : {0x1234u, 0xffffu, 0u}) {
        topp->src = x;
        topp->eval();
        if (topp->a_o != x) {
            std::printf("%%Error: src=%x a_o=%x exp=%x\n", x, topp->a_o, x);
            ++fails;
        }
        if (topp->w_o != (~x & 0xffffu)) {
            std::printf("%%Error: src=%x w_o=%x exp=%x\n", x, topp->w_o, ~x & 0xffffu);
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
