// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL interface view ports with converse and view-of forms
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
    for (int xin = 0; xin < 256; xin += 37) {
        const int yin = (xin * 5 + 3) & 0xff;
        topp->xin = xin;
        topp->yin = yin;
        topp->eval();
        const int xexp = (xin + 1) & 0xff;
        const int yexp = (yin + 5) & 0xff;
        const int xouts[3] = {topp->xout, topp->xout2, topp->xout3};
        const int youts[3] = {topp->yout, topp->yout2, topp->yout3};
        for (int inst = 0; inst < 3; ++inst) {
            if (xouts[inst] != xexp || youts[inst] != yexp) {
                std::printf("%%Error: instance %d xin=%d yin=%d x=%d y=%d\n", inst, xin, yin,
                            xouts[inst], youts[inst]);
                ++fails;
            }
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
