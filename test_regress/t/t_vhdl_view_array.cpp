// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL interface views of an array of records, on a top entity
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
    for (int bits = 0; bits < 64; ++bits) {
        const int v1 = bits & 1;
        const int v2 = (bits >> 1) & 1;
        const int rdy = (bits >> 2) & 1;
        const int d1 = (bits * 37) & 0xff;
        const int d2 = (bits * 91 + 5) & 0xff;
        topp->b__tx__1__valid = v1;
        topp->b__tx__1__data = d1;
        topp->b__tx__2__valid = v2;
        topp->b__tx__2__data = d2;
        topp->b__rx__ready = rdy;
        topp->arr__0__valid = (bits >> 3) & 1;
        topp->arr__1__valid = (bits >> 4) & 1;
        topp->arr__2__valid = (bits >> 5) & 1;
        topp->eval();
        if (topp->b__tx__1__ready != rdy || topp->b__tx__2__ready != rdy
            || topp->b__rx__valid != (v1 & v2) || topp->b__rx__data != (d1 ^ d2)) {
            std::printf("%%Error: bus bits=%d\n", bits);
            ++fails;
        }
        if (topp->arr__0__ready != 1 || topp->arr__1__ready != ((bits >> 3) & 1)
            || topp->arr__2__ready != (((bits >> 4) & 1) & ((bits >> 5) & 1))) {
            std::printf("%%Error: array bits=%d\n", bits);
            ++fails;
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
