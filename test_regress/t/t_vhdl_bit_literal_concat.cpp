// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL bit-string literal concatenated with a bit_vector
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
    for (int x = 0; x < 256; ++x) {
        for (int s = 0; s < 2; ++s) {
            topp->d = x;
            topp->s = s;
            topp->eval();
            const int want[]
                = {x >> 3, (8 << 11) | ((x & 0x3f) << 5), s ? x >> 2 : x, (x << 4) | 5};
            const int got[] = {topp->o1, topp->o2, topp->o3, topp->o4};
            for (int i = 0; i < 4; ++i) {
                if (got[i] != want[i]) {
                    std::printf("%%Error: d=%d s=%d output %d = 0x%x expected 0x%x\n", x, s, i + 1,
                                got[i], want[i]);
                    ++errors;
                }
            }
        }
    }

    topp->final();
    delete topp;
    if (errors) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
