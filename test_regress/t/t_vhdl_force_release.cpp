// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL force and release of a signal test
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
    auto check = [&](int want, const char* what) {
        topp->eval();
        if (topp->q != want) {
            std::printf("%%Error: %s: q=%d want %d\n", what, static_cast<int>(topp->q), want);
            ++fails;
        }
        // `force out` acts as `force`: 6 while forced, else d
        const int want2 = topp->en ? 6 : topp->d;
        if (topp->q2 != want2) {
            std::printf("%%Error: %s: q2=%d want %d\n", what, static_cast<int>(topp->q2), want2);
            ++fails;
        }
    };

    topp->en = 0;
    topp->d = 3;
    check(3, "driven");
    topp->en = 1;
    check(10, "forced");
    topp->d = 5;
    check(10, "forced, driver changes");
    topp->en = 0;
    check(5, "released");
    delete topp;
    return fails ? 1 : 0;
}
