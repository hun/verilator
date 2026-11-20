// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a scalar character literal in a vector context
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

    // The literal combines element-wise with the vector, whatever the vector
    // value is (these are the values GHDL reports).
    int fails = 0;
    static const int vals[4] = {0xFF, 0xA5, 0x00, 0x5A};
    for (const int v : vals) {
        topp->v = v;
        topp->eval();
        if (topp->y1 != v) {
            std::printf("%%Error: v=%02x y1=%02x exp=%02x\n", v, topp->y1, v);
            ++fails;
        }
        if (topp->y2 != v) {
            std::printf("%%Error: v=%02x y2=%02x exp=%02x\n", v, topp->y2, v);
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
