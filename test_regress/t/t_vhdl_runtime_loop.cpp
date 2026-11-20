// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a run-time loop with exit, a slice with a run-time width, and a
// conversion whose width is a run-time value
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
    int fails = 0;
    for (unsigned v = 0; v < 256; ++v) {
        topp->din = v;
        topp->eval();
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
        int expectedAddr = 0;
        bool expectedFound = false;
        for (int i = 0; i < 8; ++i) {
            if ((v >> i) & 1) {
                expectedAddr = i;
                expectedFound = true;
                break;
            }
        }
        const unsigned hi = v & 3u;
        const unsigned expectedSlice = 3u & ((1u << (hi + 1)) - 1u);
        if (static_cast<bool>(topp->found) != expectedFound
            || (expectedFound && static_cast<unsigned>(topp->addr) != static_cast<unsigned>(expectedAddr))) {
            std::printf("%%Error: din=%02x found=%d addr=%u expected %d/%d\n", v,
                        static_cast<int>(topp->found), static_cast<unsigned>(topp->addr),
                        expectedFound ? 1 : 0, expectedAddr);
            ++fails;
        }
        if (static_cast<unsigned>(topp->sliced) != expectedSlice) {
            std::printf("%%Error: din=%02x sliced=%02x expected %02x\n", v,
                        static_cast<unsigned>(topp->sliced), expectedSlice);
            ++fails;
        }
        if (fails > 8) break;
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
