// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: constants of a package body, and non-integer constants built
// by calling functions another package defines in its body
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
    const bool ok = topp->o != 0;
    topp->final();
    delete topp;
    if (!ok) {
        std::printf("%%Error: the constant built in the package body is not '3!' and 0011\n");
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
