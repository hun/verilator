// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a qualified expression keeps its scalar type
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

    // integer'(x) divides and compares with integer semantics, both in a
    // static fold and at run time (GHDL values)
    int fails = 0;
    struct Case final {
        int x;
        int q1;
        int q2;
    };
    static const Case cases[3] = {{-5, -2, 1}, {7, 3, 0}, {-1, 0, 1}};
    for (const Case& c : cases) {
        topp->x = c.x;
        topp->eval();
        if (topp->q1 != c.q1) {
            std::printf("%%Error: x=%d q1=%d exp=%d\n", c.x, topp->q1, c.q1);
            ++fails;
        }
        if (topp->q2 != c.q2) {
            std::printf("%%Error: x=%d q2=%d exp=%d\n", c.x, topp->q2, c.q2);
            ++fails;
        }
    }
    if (topp->c1_o != -2) {
        std::printf("%%Error: c1_o=%d exp=-2\n", topp->c1_o);
        ++fails;
    }
    if (topp->c2_o != 1) {
        std::printf("%%Error: c2_o=%d exp=1\n", topp->c2_o);
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
