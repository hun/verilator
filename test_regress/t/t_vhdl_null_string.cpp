// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an empty string as a value
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

    int fails = 0;
    // "" -> length 0, no commas, left 1, right 0: 0 + 0 + 100 + 0
    if (topp->n1 != 100 || topp->m1 != 0 || topp->p1 != 32) {
        std::printf("%%Error: n1=%u m1=%d p1=%d expected 100/0/32\n",
                    static_cast<unsigned>(topp->n1), static_cast<int>(topp->m1),
                    static_cast<int>(topp->p1));
        ++fails;
    }
    // "a,b,c" -> length 5, two commas, left 1, right 5: 5 + 20 + 100 + 5000
    if (topp->n2 != 5125 || topp->m2 != 1 || topp->p2 != 32) {
        std::printf("%%Error: n2=%u m2=%d p2=%d expected 5125/1/32\n",
                    static_cast<unsigned>(topp->n2), static_cast<int>(topp->m2),
                    static_cast<int>(topp->p2));
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
