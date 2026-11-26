// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a record compared with an aggregate
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
        unsigned eq;
    } cases[] = {{0x28, 1, 1}, {0x28, 2, 0}, {0x29, 1, 0}, {0, 0, 0}};
    for (const auto& c : cases) {
        topp->a = c.a;
        topp->b = c.b;
        topp->eval();
        if (topp->e1 != c.eq || topp->e2 != c.eq || topp->e3 == c.eq) {
            std::printf("%%Error: a=%x b=%x e1=%u e2=%u e3=%u eq=%u\n", c.a, c.b,
                        static_cast<unsigned>(topp->e1), static_cast<unsigned>(topp->e2),
                        static_cast<unsigned>(topp->e3), c.eq);
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
