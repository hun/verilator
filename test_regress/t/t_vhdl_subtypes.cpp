// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL user-defined and local subtype test
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
    topp->a = 0xa;
    topp->b = 0x5;
    topp->n = 3;
    topp->eval();
    if (static_cast<int>(topp->y) != 0xa) {
        std::printf("%%Error: y=%d\n", static_cast<int>(topp->y));
        ++fails;
    }
    if (static_cast<int>(topp->z) != 0xa) {
        std::printf("%%Error: z=%d\n", static_cast<int>(topp->z));
        ++fails;
    }
    if (static_cast<int>(topp->g) != 0xa) {
        std::printf("%%Error: g=%d\n", static_cast<int>(topp->g));
        ++fails;
    }
    if (static_cast<int>(topp->u) != 0xa) {
        std::printf("%%Error: u=%d\n", static_cast<int>(topp->u));
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
