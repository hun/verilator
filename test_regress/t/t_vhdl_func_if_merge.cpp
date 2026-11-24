// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL if arms in a function body that run on data
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static int mixer(unsigned x) {
    unsigned a = x;
    unsigned b = 5;
    for (int i = 0; i < 8; ++i) {
        if ((x >> i) & 1) {
            if (i % 2 == 0) {
                a = (a + 1) & 0xff;
            } else {
                b = (b + a) & 0xff;
            }
        } else if (i == 3) {
            b = (b - 1) & 0xff;
        } else if ((x >> (7 - i)) & 1) {
            a ^= b;
        }
    }
    return (a + b) & 0xff;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (unsigned v = 0; v < 0x10000; ++v) {
        topp->v = v;
        topp->eval();
        const int cnt = __builtin_popcount(v);
        const int mix = mixer(v & 0xff);
        if (topp->cnt != cnt || topp->mix != mix) {
            if (++fails < 10) {
                std::printf("%%Error: v=%04x cnt=%d exp=%d mix=%d exp=%d\n", v, topp->cnt, cnt,
                            topp->mix, mix);
            }
        }
    }
    if (topp->k != __builtin_popcount(0xa5f3)) {
        std::printf("%%Error: k=%d exp=%d\n", topp->k, __builtin_popcount(0xa5f3));
        ++fails;
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
