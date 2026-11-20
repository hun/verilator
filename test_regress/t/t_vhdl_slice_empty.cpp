// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a null slice on either side of an assignment
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
    topp->clk = 0;
    topp->di = 0;
    topp->eval();
    unsigned hist[4] = {0, 0, 0, 0};
    for (unsigned cycle = 1; cycle <= 20; ++cycle) {
        topp->di = cycle * 0x11;
        topp->eval();
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
        // A one-deep pipeline passes the input through after one edge
        if (topp->o1 != topp->di) {
            std::printf("%%Error: cycle %u o1=%02x expected %02x\n", cycle,
                        static_cast<unsigned>(topp->o1), static_cast<unsigned>(topp->di));
            ++fails;
        }
        // A three-deep pipeline holds the input of two cycles earlier (the first
        // input reaches the last stage after two more edges)
        if (topp->o3 != hist[1]) {
            std::printf("%%Error: cycle %u o3=%02x expected %02x\n", cycle,
                        static_cast<unsigned>(topp->o3), hist[1]);
            ++fails;
        }
        hist[1] = hist[0];
        hist[0] = topp->di;
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
