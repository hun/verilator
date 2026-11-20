// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a record element of an array local takes an aggregate
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

    // The plan is [(0, 3, 2), (6, -1, 5)]: one update and then a level whose core is spent
    int fails = 0;
    const auto check = [&](const char* name, unsigned got, unsigned want) {
        if (got != want) {
            std::printf("%%Error: %s = %08x, expected %08x\n", name, got, want);
            ++fails;
        }
    };
    check("o_base", topp->o_base, 6u);
    check("o_core", topp->o_core, 0xffffffffu);
    check("o_done", topp->o_done, 2u);
    check("o_len", topp->o_len, 2u);
    check("o_gen", topp->o_gen, 5u);
    check("o_sum", topp->o_sum, 24u);

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
