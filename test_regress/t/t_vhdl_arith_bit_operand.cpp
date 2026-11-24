// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a std_logic added to or taken from a numeric vector
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
        unsigned u;
        int s;
        unsigned b;
    } cases[] = {{5, -3, 1}, {5, -3, 0}, {0, 7, 1}, {15, -8, 1}};
    for (const auto& c : cases) {
        topp->u = c.u;
        topp->s = c.s & 0xF;
        topp->b = c.b;
        topp->eval();
        const auto wrap = [](int v) { return static_cast<unsigned>(v) & 0xF; };
        if (topp->up != wrap(static_cast<int>(c.u + c.b))
            || topp->um != wrap(static_cast<int>(c.u) - static_cast<int>(c.b))
            || topp->sp != wrap(c.s + static_cast<int>(c.b))
            || topp->sm != wrap(c.s - static_cast<int>(c.b))) {
            std::printf("%%Error: u=%u s=%d b=%u up=%x um=%x sp=%x sm=%x\n", c.u, c.s, c.b,
                        static_cast<unsigned>(topp->up), static_cast<unsigned>(topp->um),
                        static_cast<unsigned>(topp->sp), static_cast<unsigned>(topp->sm));
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
