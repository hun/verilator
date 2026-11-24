// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a procedure the function declares and calls
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

    const int expect[16]
        = {10, 11, 12, 100, 101, 7, 8, 9, 10, 200, 201, 0xaa, 0xaa, 0xff, 0xff, 2};
    int fails = 0;
    for (int i = 0; i < 16; ++i) {
        topp->sel = i;
        topp->eval();
        if (topp->q != expect[i]) {
            std::printf("%%Error: tbl(%d)=%d exp=%d\n", i, topp->q, expect[i]);
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
