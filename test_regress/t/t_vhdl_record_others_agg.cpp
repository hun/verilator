// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a record aggregate of only others
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
    const struct {
        unsigned a;
        unsigned b;
        unsigned zero;
    } cases[] = {{0, 0, 1}, {1, 0, 0}, {0, 2, 0}, {5, 5, 0}};
    for (const auto& c : cases) {
        topp->a = c.a;
        topp->b = c.b;
        topp->eval();
        if (topp->zero != c.zero || topp->sum != 6) {
            std::printf("%%Error: a=%u b=%u zero=%u sum=%u exp=%u 6\n", c.a, c.b,
                        static_cast<unsigned>(topp->zero), static_cast<unsigned>(topp->sum),
                        c.zero);
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
