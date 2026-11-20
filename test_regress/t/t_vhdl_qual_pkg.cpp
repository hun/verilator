// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL qualified package type reference
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

    const CData iaVal[2] = {0xA, 0xF};
    const CData ibVal[2] = {0x6, 0x3};
    const CData oExp[2] = {0x2, 0x3};  // ri.a and ri.b

    int fails = 0;
    for (int i = 0; i < 2; ++i) {
        topp->ia = iaVal[i];
        topp->ib = ibVal[i];
        topp->eval();
        const int got = topp->o;
        if (got != oExp[i]) {
            std::printf("%%Error: ia=%x ib=%x o=%x exp=%x\n", iaVal[i], ibVal[i], got, oExp[i]);
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
