// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL wait until on a function call that needs a temporary
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static unsigned hash(unsigned x) {
    unsigned v = x & 0xff;
    for (int i = 0; i < 4; ++i) {
        v ^= (v << 3) & 0xff;
        v ^= v >> 2;
    }
    return v;
}

int main(int argc, char** argv) {
    VerilatedContext* const contextp = new VerilatedContext;
    contextp->commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX{contextp};

    int fails = 0;
    topp->a = 0;
    topp->eval();
    // Each new value of a is an event; the process takes the first value that satisfies the
    // condition it waits for
    unsigned state = 0;
    unsigned exp1 = 0;
    unsigned exp2 = 0;
    unsigned prev = 0;
    for (unsigned step = 1; step < 2000; ++step) {
        const unsigned value = (step * 73 + 5) & 0xff;
        if (value == prev) continue;
        prev = value;
        contextp->timeInc(1);
        topp->a = value;
        topp->eval();
        const unsigned h = hash(value);
        if (state == 0) {
            if (h & 1) {
                exp1 = h;
                state = 1;
            }
        } else if (!(h & 1)) {
            exp2 = h;
            state = 0;
        }
        if (topp->y1 != exp1 || topp->y2 != exp2) {
            if (++fails < 10) {
                std::printf("%%Error: a=%02x y1=%02x/%02x y2=%02x/%02x\n", value, topp->y1, exp1,
                            topp->y2, exp2);
            }
        }
    }

    topp->final();
    delete topp;
    delete contextp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
