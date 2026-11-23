// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL enumeration and boolean port actuals of an instance
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
    for (int x = 0; x < 2; ++x) {
        topp->x = x;
        topp->eval();
        // The generic g is b_32 (1); the signal is c_64 (2) or a_16 (0); the boolean
        // generic is true and the boolean expression follows x; values from GHDL
        if (topp->o1 != 1 || topp->o2 != (x ? 2 : 0) || topp->ob1 != 1 || topp->ob2 != x) {
            std::printf("%%Error: x=%d o1=%d o2=%d ob1=%d ob2=%d\n", x, topp->o1, topp->o2,
                        topp->ob1, topp->ob2);
            ++errors;
        }
    }

    topp->final();
    delete topp;
    if (errors) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
