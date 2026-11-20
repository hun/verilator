// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a constant of an unconstrained array of records
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
    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    topp->clk = 0;
    topp->eval();
    int fails = 0;
    if (static_cast<unsigned>(topp->o) != 2u) {
        std::printf("%%Error: o = %u, expected 2\n", static_cast<unsigned>(topp->o));
        ++fails;
    }
    if (static_cast<unsigned>(topp->o2) != 7u) {
        std::printf("%%Error: o2 = %u, expected 7\n", static_cast<unsigned>(topp->o2));
        ++fails;
    }
    // Three elements, from the aggregate's length
    if (static_cast<unsigned>(topp->len) != 3u) {
        std::printf("%%Error: len = %u, expected 3\n", static_cast<unsigned>(topp->len));
        ++fails;
    }
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
