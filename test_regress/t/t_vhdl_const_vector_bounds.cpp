// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL unconstrained vector constant takes its bounds from its value
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

    // Values as GHDL gives them for the default generics
    int fails = 0;
    const auto check = [&](const char* name, unsigned got, unsigned exp) {
        if (got != exp) {
            std::printf("%%Error: %s=%x exp=%x\n", name, got, exp);
            ++fails;
        }
    };
    check("cat_o", topp->cat_o, 0xab9e);
    check("len_o", topp->len_o, 16);
    check("top_o", topp->top_o, 1);
    check("low_o", topp->low_o, 0);
    check("sum_o", topp->sum_o, 0xac);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
