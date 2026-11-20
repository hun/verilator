// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL character and extended enumeration literals test
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
    // my_enum is (normal, 'z', \ext\, \long\\ext\): position 0, 1, 2, 3.
    for (int sel = 0; sel < 4; ++sel) {
        topp->sel = sel;
        topp->eval();
        topp->eval();  // Let the combinational output settle
        const int got = static_cast<int>(topp->pos_o);
        if (got != sel) {
            std::printf("%%Error: sel=%d pos_o=%d expected %d\n", sel, got, sel);
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
