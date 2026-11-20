// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an unconstrained vector constant and generic take their width
//              from the literal that defaults them
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
    // 00111 and 00101, both five bits wide
    const int got = static_cast<int>(topp->o);
    topp->final();
    delete topp;

    if (got != 0x5) {
        std::printf("%%Error: o=%x expected 5\n", got);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}