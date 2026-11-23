// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL std_logic_unsigned arithmetic with a std_logic operand
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
    // Expected values come from GHDL with -fsynopsys
    const int got[5] = {topp->a, topp->b, topp->c, topp->d, topp->e};
    const int want[5] = {0, 15, 7, 9, 4};
    int errors = 0;
    for (int i = 0; i < 5; ++i) {
        if (got[i] != want[i]) {
            std::printf("%%Error: output %d = %d expected %d\n", i, got[i], want[i]);
            ++errors;
        }
    }

    topp->final();
    delete topp;
    if (errors) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
