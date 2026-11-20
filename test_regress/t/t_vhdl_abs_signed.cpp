// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL abs on a numeric_std signed vector
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
    // abs("11111110") = abs(-2) = 2 (GHDL)
    topp->s = 0xFE;
    topp->eval();
    if (topp->a1 != 2) {
        std::printf("%%Error: a1=%d exp=2\n", topp->a1);
        ++fails;
    }
    // abs of a positive value stays the value
    topp->s = 0x03;
    topp->eval();
    if (topp->a1 != 3) {
        std::printf("%%Error: a1=%d exp=3\n", topp->a1);
        ++fails;
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
