// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL bit-string literals and bit scalars concatenated with a bit_vector
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

    int errors = 0;
    for (int x = 0; x < 64; ++x) {
        const int f = (x >> 5) & 1;
        const int n = (x >> 4) & 1;
        const int sk = x & 15;
        topp->f = f;
        topp->n = n;
        topp->sk = sk;
        topp->eval();
        // Expected values come from GHDL
        const int want[]
            = {(f << 7) | (n << 6) | sk, (f << 9) | (n << 8) | (sk << 2) | 2, 0x20 | sk};
        const int got[] = {topp->o1, topp->o2, topp->o3};
        for (int i = 0; i < 3; ++i) {
            if (got[i] != want[i]) {
                std::printf("%%Error: x=%d output %d = 0x%x expected 0x%x\n", x, i + 1, got[i],
                            want[i]);
                ++errors;
            }
        }
    }

    topp->final();
    delete topp;
    if (errors) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
