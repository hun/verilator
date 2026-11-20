// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL block, generate and process declarative parts test
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
    for (int a = 0; a < 16; ++a) {
        topp->i_a = a;
        topp->eval();
        // Every declarative part forwards i_a through its own constant-sized object
        const struct {
            const char* name;
            int got;
        } checks[] = {{"o_blk", static_cast<int>(topp->o_blk)},
                      {"o_gen", static_cast<int>(topp->o_gen)},
                      {"o_pro", static_cast<int>(topp->o_pro)}};
        for (const auto& check : checks) {
            if (check.got != a) {
                std::printf("%%Error: i_a=%d: %s=%d expected %d\n", a, check.name, check.got, a);
                ++fails;
            }
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
