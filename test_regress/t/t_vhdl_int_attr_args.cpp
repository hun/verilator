// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL succ, pred, leftof, rightof, pos and val of an integer type
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
    for (int i = 2; i <= 9; ++i) {
        topp->i = i;
        topp->eval();
        // Expected values come from GHDL
        const int want[] = {i + 1, i - 1, i - 1, i + 1, i + 1, i - 1, i, i, 553194};
        const int got[]
            = {static_cast<int>(topp->o1), static_cast<int>(topp->o2), static_cast<int>(topp->o3),
               static_cast<int>(topp->o4), static_cast<int>(topp->o5), static_cast<int>(topp->o6),
               static_cast<int>(topp->o7), static_cast<int>(topp->o8), static_cast<int>(topp->s)};
        for (int n = 0; n < 9; ++n) {
            if (got[n] != want[n]) {
                std::printf("%%Error: i=%d output %d = %d expected %d\n", i, n + 1, got[n],
                            want[n]);
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
