// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL predefined array comparison and numeric_std comparison
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

    topp->eval();
    // 1,4,5,6,8 set; 0,2,3,7,9 clear
    const int got = static_cast<int>(topp->o);
    const int want = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 8);
    if (got != want) {
        std::printf("%%Error: o=0x%x expected 0x%x\n", got, want);
        topp->final();
        delete topp;
        return 1;
    }
    const int uGot = static_cast<int>(topp->u);
    const int uWant = (1 << 1) | (1 << 2);
    if (uGot != uWant) {
        std::printf("%%Error: u=0x%x expected 0x%x\n", uGot, uWant);
        topp->final();
        delete topp;
        return 1;
    }
    const int wGot = static_cast<int>(topp->w);
    const int wWant = (1 << 1) | (1 << 5) | (1 << 6) | (1 << 7);
    if (wGot != wWant) {
        std::printf("%%Error: w=0x%x expected 0x%x\n", wGot, wWant);
        topp->final();
        delete topp;
        return 1;
    }

    topp->final();
    delete topp;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
