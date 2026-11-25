// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL process variables that read themselves in every assignment
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static unsigned model(unsigned a) {
    unsigned v = a;
    for (int i = 0; i < 12; ++i) {
        v ^= (v << 3) & 0xff;
        v ^= v >> 2;
    }
    return v;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (unsigned x = 0; x < 256; ++x) {
        topp->a = x;
        topp->eval();
        const unsigned exp = model(x);
        if (topp->y != exp) {
            if (++fails < 10) std::printf("%%Error: a=%02x y=%02x exp=%02x\n", x, topp->y, exp);
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
