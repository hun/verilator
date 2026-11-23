// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL positional then named port and generic associations
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
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 16; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();
            // u1 has k = 1 and ands its inputs, u2 has k = 2 and ors them; values from GHDL
            if (topp->y1 != (a & b) || topp->y2 != (a | b)) {
                std::printf("%%Error: a=%d b=%d y1=%d y2=%d\n", a, b, topp->y1, topp->y2);
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
