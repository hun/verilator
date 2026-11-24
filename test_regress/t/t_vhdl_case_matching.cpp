// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL case_matching test
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

    const int want[8] = {0, 1, 2, 2, 3, 3, 3, 3};
    int fails = 0;
    for (int i = 0; i < 8; ++i) {
        topp->i = i;
        topp->eval();
        if (topp->o != want[i]) {
            std::printf("%%Error: i=%d o=%d want %d\n", i, static_cast<int>(topp->o), want[i]);
            ++fails;
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
