// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL function calls that need a temporary in every statement position
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static unsigned hash(unsigned x) {
    unsigned v = x & 0xff;
    for (int i = 0; i < 4; ++i) {
        v ^= (v << 3) & 0xff;
        v ^= v >> 2;
    }
    return v;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (unsigned a = 0; a < 256; ++a) {
        // Every b for a few a, and one b for the rest
        for (unsigned b = (a % 16 == 0 ? 0 : (a * 37 + 11) & 0xff); b < 256; ++b) {
            if (a % 16 != 0 && b != ((a * 37 + 11) & 0xff)) break;
            topp->a = a;
            topp->b = b;
            topp->eval();
            unsigned x = a;
            int n = 0;
            while (hash(x) != 0x3c && n < 40) {
                x = (x + 1) & 0xff;
                ++n;
            }
            const unsigned y1 = x;
            x = b;
            n = 0;
            while (!(hash(x) & 1 || n == 20)) {
                x = (x + 3) & 0xff;
                ++n;
            }
            const unsigned y2 = x;
            unsigned y3 = 0;
            for (unsigned i = 0; i < 8; ++i) y3 += (hash((b + i) & 0xff) >> 1) & 1;
            const unsigned y5 = hash(a) == 0 ? 1 : (hash(b) >> 7) & 1 ? 2 : hash(a ^ b);
            unsigned y6 = a;
            for (unsigned i = 0; i <= 5000; ++i) y6 = hash(y6 ^ (i % 256));
            const unsigned yc1 = hash(b) == 0 ? a : b;
            const unsigned sel = hash(a) & 3;
            const unsigned yc2 = sel == 0 ? 0xaa : sel == 1 ? 0xbb : 0xcc;
            const unsigned exp4 = sel == 0 ? 0x11 : sel == 1 ? 0x22 : sel == 2 ? 0x33 : 0x44;
            if (topp->y1 != y1 || topp->y2 != y2 || topp->y3 != y3 || topp->y4 != exp4
                || topp->y5 != y5 || topp->y6 != y6 || topp->yc1 != yc1 || topp->yc2 != yc2) {
                if (++fails < 10) {
                    std::printf("%%Error: a=%02x b=%02x y=%02x,%02x,%02x,%02x,%02x,%02x c=%02x,%02x"
                                " exp=%02x,%02x,%02x,%02x,%02x,%02x c=%02x,%02x\n",
                                a, b, topp->y1, topp->y2, topp->y3, topp->y4, topp->y5, topp->y6,
                                topp->yc1, topp->yc2, y1, y2, y3, exp4, y5, y6, yc1, yc2);
                }
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
