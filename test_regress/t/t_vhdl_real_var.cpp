// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a constant expression with real operands folds at elaboration
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    topp->i_in = 10;  // sig_r = 10 / 4.0 = 2.5
    topp->eval();
    int fails = 0;
    const auto check = [&](const char* name, uint32_t got, uint32_t want) {
        if (got != want) {
            std::printf("%%Error: %s=%u expected %u\n", name, got, want);
            ++fails;
        }
    };
    check("o_scaled", topp->o_scaled, 5u);
    check("o_round", topp->o_round, 0xfffffffdu);  // -3
    check("o_cmp", topp->o_cmp, 0x7u);
    check("o_sig", topp->o_sig, 5u);  // integer(2.5 * 2.0)
    check("o_pow", topp->o_pow, 16u);  // integer(2.5 ** 3) = 15.625
    check("o_powv", topp->o_powv, 6u);  // integer(2.5 ** 2) = 6.25

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
