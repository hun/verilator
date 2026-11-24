// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL an array index range written with attributes
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
    const auto check = [&fails](const char* name, int sel, int got, int expect) {
        if (got != expect) {
            std::printf("%%Error: %s(%d)=0x%x exp=0x%x\n", name, sel, got, expect);
            ++fails;
        }
    };
    for (int sel = 0; sel < 4; ++sel) {
        topp->sel = sel;
        topp->eval();
        check("up", sel, topp->q_up, 0x10 + sel);  // 0 to 3: index 0 is the first element
        check("dn", sel, topp->q_dn, 0x23 - sel);  // 3 downto 0: index 3 is the first
        check("rev", sel, topp->q_rev, 0x30 + sel);  // 'reverse_range of 3 downto 0
        check("rng", sel, topp->q_rng, 0x43 - sel);  // 'range is 3 downto 0
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
