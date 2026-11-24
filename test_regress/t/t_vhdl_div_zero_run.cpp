// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a division by zero that runs
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
    // Divisions that run with a nonzero divisor, and one a test keeps from a zero divisor
    topp->a = 42;
    topp->b = 5;
    topp->guard = 0;
    topp->eval();
    if (topp->plain != 8 || Verilated::gotFinish()) {
        std::printf("%%Error: plain=%d\n", static_cast<int>(topp->plain));
        ++fails;
    }
    topp->guard = 1;
    topp->b = 0;
    topp->eval();
    if (topp->skipped != 0 || Verilated::gotFinish()) {
        std::printf("%%Error: the skipped division ran or ended the run\n");
        ++fails;
    }
    // The division that runs with a zero divisor ends the run
    topp->guard = 0;
    topp->eval();
    if (!Verilated::gotFinish()) {
        std::printf("%%Error: division by zero did not end the run\n");
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
