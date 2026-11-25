// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL case arms in a function body that run on data
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static unsigned mix(unsigned x) {
    unsigned a = x;
    unsigned b = 5;
    for (int i = 0; i < 24; ++i) {
        const int p = i % 7;
        switch ((x >> p) & 3) {
        case 0: a = (a + 1) & 0xff; break;
        case 1:
        case 2:
            b ^= a;
            if ((x >> p) & 1) a = (a << 1) & 0xff;
            break;
        default:
            a ^= b;
            b = (b + 3) & 0xff;
            break;
        }
        if ((x >> ((p + 2) % 8)) & 1) b = (b + 1) & 0xff;
    }
    return (a ^ b) & 0xff;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (unsigned x = 0; x < 256; ++x) {
        topp->a = x;
        topp->eval();
        const unsigned exp = mix(x);
        if (topp->yc != exp || topp->yp != exp) {
            if (++fails < 10) {
                std::printf("%%Error: a=%02x yc=%02x yp=%02x exp=%02x\n", x, topp->yc, topp->yp,
                            exp);
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
