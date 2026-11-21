// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a selected assignment with a computed choice
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
    // 0 -> "0001", 1 -> "0010", 2 -> "0100", everything else -> "1111"
    static const int expect[] = {1, 2, 4, 15, 15, 15, 15, 15};
    for (int value = 0; value < 8; ++value) {
        topp->sel = value;
        topp->eval();
        if (topp->o != expect[value]) {
            std::printf("%%Error: sel=%d o=%d exp=%d\n", value, topp->o, expect[value]);
            ++fails;
        }
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
