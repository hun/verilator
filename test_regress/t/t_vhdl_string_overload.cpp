// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: string actuals in an overloaded call, and a string constant as
// a generic actual
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
    int fails = 0;
    // A and B differ, so the string overload returns B ("same", 4), plus "bbb" (3)
    if (topp->from_overload != 7) {
        std::printf("%%Error: from_overload=%u expected 7\n",
                    static_cast<unsigned>(topp->from_overload));
        ++fails;
    }
    if (topp->from_integer != 3) {
        std::printf("%%Error: from_integer=%d expected 3\n",
                    static_cast<int>(topp->from_integer));
        ++fails;
    }
    // "hello" (5) + Width (3) through the string generic
    if (topp->from_constant != 8) {
        std::printf("%%Error: from_constant=%u expected 8\n",
                    static_cast<unsigned>(topp->from_constant));
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
