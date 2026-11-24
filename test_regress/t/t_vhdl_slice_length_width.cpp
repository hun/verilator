// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: the length of a slice as a conversion width
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
        unsigned c;
        unsigned o;
    } cases[] = {{0x0F0, 1, 0x101}, {0x000, 0, 0x000}, {0x1FF, 1, 0x010}, {0x001, 1, 0x012}};
    for (const auto& k : cases) {
        topp->a = k.a;
        topp->c = k.c;
        topp->eval();
        if (topp->o != k.o) {
            std::printf("%%Error: a=%x c=%u o=%x exp=%x\n", k.a, k.c,
                        static_cast<unsigned>(topp->o), k.o);
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
