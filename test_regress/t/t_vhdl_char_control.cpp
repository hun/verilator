// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL char_control test
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
    for (int sel = 0; sel < 2; ++sel) {
        topp->sel = sel;
        topp->eval();
        if (topp->q != sel) {
            std::printf("%%Error: sel=%d q=%d\n", sel, static_cast<int>(topp->q));
            ++fails;
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
