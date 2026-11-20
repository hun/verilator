// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL logical operators mixing a vector and a scalar bit
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
    const int slvVals[4] = {0xA, 0x5, 0x0, 0xF};
    for (const int slv : slvVals) {
        for (int s = 0; s <= 1; ++s) {
            topp->slv = slv;
            topp->s = s;
            topp->eval();

            const int qand = static_cast<int>(topp->qand);
            const int qor = static_cast<int>(topp->qor);
            const int qnand = static_cast<int>(topp->qnand);
            const int qleft = static_cast<int>(topp->qleft);
            const int qandExp = s ? slv : 0;
            const int qorExp = s ? 0xF : slv;
            const int qnandExp = s ? (~slv) & 0xF : 0xF;
            if (qand != qandExp) {
                std::printf("%%Error: slv=%x s=%d qand=%x exp=%x\n", slv, s, qand, qandExp);
                ++fails;
            }
            if (qor != qorExp) {
                std::printf("%%Error: slv=%x s=%d qor=%x exp=%x\n", slv, s, qor, qorExp);
                ++fails;
            }
            if (qnand != qnandExp) {
                std::printf("%%Error: slv=%x s=%d qnand=%x exp=%x\n", slv, s, qnand, qnandExp);
                ++fails;
            }
            if (qleft != qandExp) {
                std::printf("%%Error: slv=%x s=%d qleft=%x exp=%x\n", slv, s, qleft, qandExp);
                ++fails;
            }
        }
    }

    for (int b = 0; b <= 1; ++b) {
        topp->bv = 0x9;
        topp->b = b;
        topp->eval();
        const int qb = static_cast<int>(topp->qb);
        const int qbExp = b ? 0x9 : 0;
        if (qb != qbExp) {
            std::printf("%%Error: b=%d qb=%x exp=%x\n", b, qb, qbExp);
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
