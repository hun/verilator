// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL real generic/constant comparisons
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

    const struct {
        const char* name;
        int got;
        int exp;
    } checks[] = {
        {"lt", topp->lt, 1}, {"le", topp->le, 1}, {"eq", topp->eq, 0},
        {"ge", topp->ge, 0}, {"gt", topp->gt, 0}, {"bits", topp->bits, 4},
    };

    int fails = 0;
    for (const auto& c : checks) {
        if (c.got != c.exp) {
            std::printf("%%Error: %s=%d exp=%d\n", c.name, c.got, c.exp);
            ++fails;
        }
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
