// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a generic map in the binding of a configuration
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
    for (unsigned in = 0; in < 4; ++in) {
        topp->x = in & 1;
        topp->y = in >> 1;
        topp->eval();
        const unsigned xorv = (in & 1) ^ (in >> 1);
        // plain keeps its own map, the two bound ones are inverted
        if (topp->plain != xorv || topp->inverted != !xorv || topp->overridden != !xorv
            || topp->nested != !xorv) {
            std::printf("%%Error: in=%u plain=%d inverted=%d overridden=%d nested=%d\n", in,
                        static_cast<int>(topp->plain), static_cast<int>(topp->inverted),
                        static_cast<int>(topp->overridden), static_cast<int>(topp->nested));
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
