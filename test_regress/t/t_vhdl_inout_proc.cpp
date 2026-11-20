// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL inout port driven from a clocked process
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
    const auto check = [&](int en, int d, int got, int want) {
        if (got != want) {
            std::printf("%%Error: en=%d d=%d y=%d expected %d\n", en, d, got, want);
            ++fails;
        }
    };

    topp->clk = 0;
    topp->en = 0;
    topp->d = 0;
    topp->eval();
    for (int en = 0; en < 2; ++en) {
        for (int d = 0; d < 2; ++d) {
            topp->en = en;
            topp->d = d;
            topp->clk = 0;
            topp->eval();
            topp->clk = 1;
            topp->eval();
            // Released, the undriven bus reads 0 in the two-state model
            check(en, d, static_cast<int>(topp->y), en ? d : 0);
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
