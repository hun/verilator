// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL one element of an array a function returns
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

    int fails = 0;
    const auto check = [&fails](const char* name, int got, int expect) {
        if (got != expect) {
            std::printf("%%Error: %s=%d exp=%d\n", name, got, expect);
            ++fails;
        }
    };
    check("o", topp->o, 0xc);  // row(c, 0)(0)
    check("p", topp->p, 0x3);  // row(c, 0)(1)
    check("n", topp->n, 2);    // c'length

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
