// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an unconstrained vector generic
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
    if (static_cast<unsigned>(topp->o1) != 0x30u || topp->n1 != 63) {
        std::printf("%%Error: o1=%02x n1=%d expected 30/63\n", static_cast<unsigned>(topp->o1),
                    static_cast<int>(topp->n1));
        ++fails;
    }
    if (static_cast<unsigned>(topp->o2) != 0xau || topp->n2 != 3) {
        std::printf("%%Error: o2=%x n2=%d expected a/3\n", static_cast<unsigned>(topp->o2),
                    static_cast<int>(topp->n2));
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
