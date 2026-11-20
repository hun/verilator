// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a concurrent assertion in an entity statement part
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
    // The assertion fails while d is low; severity error reports and carries on
    topp->d = 0;
    topp->eval();
    if (topp->q != 0) {
        std::printf("%%Error: q=%d exp=0\n", topp->q);
        ++fails;
    }
    if (Verilated::gotFinish()) {
        std::printf("%%Error: an error-severity assertion ended the run\n");
        ++fails;
    }
    topp->d = 1;
    topp->eval();
    if (topp->q != 1) {
        std::printf("%%Error: q=%d exp=1\n", topp->q);
        ++fails;
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}