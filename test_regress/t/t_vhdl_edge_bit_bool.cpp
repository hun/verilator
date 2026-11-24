// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL edge_bit_bool test
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
    const auto check = [&](const char* what, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s got %d want %d\n", what, got, want);
            ++fails;
        }
    };
    topp->clk = 0;
    topp->ck = 0;
    topp->d = 1;
    topp->eval();
    check("q before edge", topp->q, 0);
    check("n before edge", topp->n, 0);
    topp->clk = 1;  // Rising edge of the bit takes d
    topp->ck = 1;  // Rising edge of the boolean does nothing
    topp->eval();
    check("q after rising", topp->q, 1);
    check("n after rising", topp->n, 0);
    topp->d = 0;
    topp->ck = 0;  // Falling edge of the boolean takes d
    topp->eval();
    check("q holds", topp->q, 1);
    check("n after falling", topp->n, 0);
    topp->d = 1;
    topp->eval();
    topp->ck = 1;
    topp->eval();
    topp->ck = 0;
    topp->eval();
    check("n after second falling", topp->n, 1);

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
