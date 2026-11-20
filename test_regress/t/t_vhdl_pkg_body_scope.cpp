// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL package body sees its specification's declarations
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
    // rotate_and_mask: the argument rotated left twice (its bit 2 lands in bit 0, which
    // is all the body's mask keeps), either directly or of the inverted argument. The
    // rest comes from the constants the body completes or reads from its specification.
    const struct {
        int a;
        int d;
        int shift;
    } vectors[] = {{1, 0b0100, 1}, {0, 0b0100, 0}, {0, 0b1011, 1}, {1, 0b1111, 1}};
    for (const auto& vector : vectors) {
        topp->i_a = vector.a;
        topp->i_d = vector.d;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_shift", static_cast<int>(topp->o_shift), vector.shift},
                      {"o_mask", static_cast<int>(topp->o_mask), 1},
                      {"o_const", static_cast<int>(topp->o_const), 24},
                      {"o_width", static_cast<int>(topp->o_width), 6}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_a=%d i_d=%d: %s=%d expected %d\n", vector.a, vector.d,
                            check.name, check.got, check.want);
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
