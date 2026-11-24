// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: loops in a function body that end on a condition
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
        unsigned in;
        unsigned out;
    } cases[] = {{0x4, 2}, {0x1, 0}, {0x8, 3}, {0x0, 4}, {0xA, 1}, {0xF, 0}};
    for (const auto& c : cases) {
        topp->v = c.in;
        topp->eval();
        if (topp->a != c.out || topp->b != c.out || topp->c != c.out) {
            std::printf("%%Error: v=%x a=%d b=%d c=%d exp=%u\n", c.in, static_cast<int>(topp->a),
                        static_cast<int>(topp->b), static_cast<int>(topp->c), c.out);
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
