// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: string generics selecting generate branches
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
    for (unsigned v = 0; v < 16; ++v) {
        topp->i = v;
        topp->eval();
        const unsigned pass = static_cast<unsigned>(topp->o_pass);
        const unsigned inv = static_cast<unsigned>(topp->o_inv);
        const unsigned def = static_cast<unsigned>(topp->o_def);
        if (pass != v || def != v || inv != ((~v) & 0xfu)) {
            std::printf("%%Error: i=%x pass=%x inv=%x def=%x\n", v, pass, inv, def);
            ++fails;
        }
        if (topp->e_pass != 1 || topp->e_inv != 0) {
            std::printf("%%Error: i=%x empty flags %d %d expected 1 0\n", v,
                        static_cast<int>(topp->e_pass), static_cast<int>(topp->e_inv));
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
