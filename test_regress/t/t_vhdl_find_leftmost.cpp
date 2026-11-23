// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: find_leftmost and find_rightmost
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };

    // d is 7 downto 0, so the leftmost element is bit 7
    topp->d = 0x24;  // bits 5 and 2
    topp->a = 0x06;  // 8 to 12: element 8 is bit 4, so bits 2 and 1 are elements 10 and 11
    topp->eval();
    check("dl", topp->dl, 5);
    check("dr", topp->dr, 2);
    check("d0", topp->d0, 7);
    check("al", topp->al, 10);
    check("ar", topp->ar, 11);

    // No match gives -1
    topp->d = 0x00;
    topp->a = 0x00;
    topp->eval();
    check("dl none", topp->dl, -1);
    check("dr none", topp->dr, -1);
    check("al none", topp->al, -1);
    check("d0 all zero", topp->d0, 7);

    topp->d = 0xff;
    topp->eval();
    check("d0 none", topp->d0, -1);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
