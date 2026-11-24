// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL severity failure ends the run at the assertion
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
    Verilated::fatalOnError(false);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    const auto expect = [&](const char* what, bool got, bool want) {
        if (got != want) {
            std::printf("%%Error: %s is %d, expected %d\n", what, got, want);
            ++fails;
        }
    };
    topp->f = 0;
    topp->s = 0;
    topp->eval();
    expect("finish before f", Verilated::gotFinish(), false);
    expect("stop before s", Verilated::gotError(), false);
    topp->f = 1;
    topp->eval();
    expect("finish after f", Verilated::gotFinish(), true);
    expect("stop after f", Verilated::gotError(), false);
    topp->final();
    delete topp;

    // A finished run ignores a stop, so clear the flag and use a new model
    Verilated::gotFinish(false);
    VM_PREFIX* const stopp = new VM_PREFIX;
    stopp->f = 0;
    stopp->s = 0;
    stopp->eval();
    stopp->s = 1;
    stopp->eval();
    expect("stop after s", Verilated::gotError(), true);
    stopp->final();
    delete stopp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
