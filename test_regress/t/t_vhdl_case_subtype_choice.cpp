// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a subtype name as a case choice
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
    for (unsigned x = 0; x < 16; ++x) {
        const unsigned expY = x < 4 ? 1 : (x >= 8 && x < 12) ? 2 : 0;
        const unsigned expZ = (x & 3) < 2 ? 1 : 2;
        topp->x = x;
        topp->eval();
        if (topp->y != expY || topp->z != expZ) {
            std::printf("%%Error: x=%u y=%u z=%u exp=%u %u\n", x, static_cast<unsigned>(topp->y),
                        static_cast<unsigned>(topp->z), expY, expZ);
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
