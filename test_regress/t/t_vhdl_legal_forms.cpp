// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL legal forms the frontend refuses today
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
    topp->eval();
    // (a) 16#FF#E2 is a based literal with an exponent: 65280
    if (topp->o1 != 65280) {
        std::printf("%%Error: o1=%d exp=65280\n", topp->o1);
        ++fails;
    }
    // (b) positional open takes p2's default '1'
    if (topp->o2 != 1) {
        std::printf("%%Error: o2=%d exp=1\n", topp->o2);
        ++fails;
    }
    // (d) for i in <subtype> generate
    if (topp->o4 != 1) {
        std::printf("%%Error: o4=%d exp=1\n", topp->o4);
        ++fails;
    }
    // (e) 'view' is a legal 2008 identifier
    if (topp->o5 != 1) {
        std::printf("%%Error: o5=%d exp=1\n", topp->o5);
        ++fails;
    }
    // (f) use of one specific declaration
    if (topp->o6 != 42) {
        std::printf("%%Error: o6=%d exp=42\n", topp->o6);
        ++fails;
    }
    // (g) '(' as an enumeration literal and case choice
    if (topp->o7 != 1) {
        std::printf("%%Error: o7=%d exp=1\n", topp->o7);
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
