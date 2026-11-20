// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL record-element port associations
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

    const CData aiVal[2] = {0xA, 0xF};
    const CData biVal[2] = {0x6, 0x0};
    const CData oaExp[2] = {0x2, 0x0};  // ri.a and ri.b
    const CData obExp[2] = {0xE, 0xF};  // ri.a or ri.b

    int fails = 0;
    for (int i = 0; i < 2; ++i) {
        topp->ai = aiVal[i];
        topp->bi = biVal[i];
        topp->eval();
        const int oa = topp->oa;
        const int ob = topp->ob;
        if (oa != oaExp[i]) {
            std::printf("%%Error: case %d oa=%x exp=%x\n", i, oa, oaExp[i]);
            ++fails;
        }
        if (ob != obExp[i]) {
            std::printf("%%Error: case %d ob=%x exp=%x\n", i, ob, obExp[i]);
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
