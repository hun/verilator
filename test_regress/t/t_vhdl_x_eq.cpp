// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL std_logic identity equality
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
    const int got = static_cast<int>(topp->o);
    const int want = 15;
    if (got != want) {
        std::printf("%%Error: o=%d expected %d\n", got, want);
        topp->final();
        delete topp;
        return 1;
    }

    topp->final();
    delete topp;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
