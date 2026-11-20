// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL comparisons between a vector and an integer expression
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

    // iv = 5; uva = 5; uvb = "1011"; svb = "1011" (same bits, different signing)
    topp->iv = 5;
    topp->uva = 0x5;
    topp->uvb = 0xB;
    topp->svb = 0xB;
    topp->eval();

    int fails = 0;
    const char* const name[7]
        = {"o_uve", "o_uvn", "o_ivne", "o_ublt", "o_sblt", "o_sbeq", "o_ivgs"};
    const uint32_t got[7] = {topp->o_uve,  topp->o_uvn,  topp->o_ivne, topp->o_ublt,
                             topp->o_sblt, topp->o_sbeq, topp->o_ivgs};
    const int exp[7] = {1, 0, 0, 0, 1, 0, 1};
    for (int i = 0; i < 7; ++i) {
        if (static_cast<int>(got[i]) != exp[i]) {
            std::printf("%%Error: %s=%d exp=%d\n", name[i], got[i], exp[i]);
            ++fails;
        }
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
