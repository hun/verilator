// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL multiple architectures test
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
    if (topp->y != 1) {
        std::printf("%%Error: y=%d expected 1 (last architecture)\n", static_cast<int>(topp->y));
        topp->final();
        delete topp;
        return 1;
    }

    topp->final();
    delete topp;

    std::printf("*-* All Finished *-*\n");
    return 0;
}
