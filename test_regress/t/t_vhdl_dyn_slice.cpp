// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL slices with a dynamic offset but a static width
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
    const int nVal[2] = {0, 1};
    const int q4Exp[2] = {0xF, 0xE};  // nibbles 0 and 1 of 0xBEEF
    const int q8Exp[2] = {0xEF, 0xBE};  // bytes 0 and 1 of 0xBEEF
    for (int i = 0; i < 2; ++i) {
        topp->n = nVal[i];
        topp->d = 0xBEEF;
        topp->eval();
        const int q4 = topp->q4;
        const int q8 = topp->q8;
        if (q4 != q4Exp[i]) {
            std::printf("%%Error: n=%d q4=%x exp=%x\n", nVal[i], q4, q4Exp[i]);
            ++fails;
        }
        if (q8 != q8Exp[i]) {
            std::printf("%%Error: n=%d q8=%x exp=%x\n", nVal[i], q8, q8Exp[i]);
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
