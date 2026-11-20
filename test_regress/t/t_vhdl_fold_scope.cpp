// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL scope an inlined subprogram body evaluates in
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
    // first_over finds the first table entry above its argument: 7 is the first above 4
    // and above 6, 9 the first above 8. o_i folds during package elaboration, o_g during
    // generic binding, o_d in the architecture, o_len while the declared type of a
    // generic whose range calls the function is resolved (the seed's length is 4)
    const struct {
        int a;
        int d;
    } vectors[] = {{1, 2}, {0, 4}};
    for (const auto& vector : vectors) {
        topp->i_a = vector.a;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_i", static_cast<int>(topp->o_i), 2},
                      {"o_g", static_cast<int>(topp->o_g), 2},
                      {"o_d", static_cast<int>(topp->o_d), vector.d},
                      {"o_len", static_cast<int>(topp->o_len), 4}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_a=%d: %s=%d expected %d\n", vector.a, check.name,
                            check.got, check.want);
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
