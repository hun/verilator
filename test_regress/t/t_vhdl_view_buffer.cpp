// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL interface view with a buffer element
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
    for (int data = 0; data < 16; ++data) {
        topp->d_in = data;
        topp->top__q = data ^ 5;
        topp->top__r = 0;
        topp->eval();
        if (topp->q_out != data || topp->r_out != (~data & 0xf)) {
            std::printf("%%Error: data=%d q=%d r=%d\n", data, topp->q_out, topp->r_out);
            ++fails;
        }
        if (topp->top__d != (~(data ^ 5) & 0xf)) {
            std::printf("%%Error: data=%d top__d=%d\n", data, topp->top__d);
            ++fails;
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
