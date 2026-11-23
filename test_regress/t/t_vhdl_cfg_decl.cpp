// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL configuration declaration selects an architecture
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
    for (int i = 0; i < 2; ++i) {
        topp->i = i;
        topp->eval();
        // u1, u3 and u4 take ident, the others the default inv; the values come from GHDL
        const int want[] = {i, !i, i, i, !i};
        const int got[] = {topp->o1, topp->o2, topp->o3, topp->o4, topp->o5};
        for (int n = 0; n < 5; ++n) {
            if (got[n] != want[n]) {
                std::printf("%%Error: i=%d o%d = %d expected %d\n", i, n + 1, got[n], want[n]);
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
