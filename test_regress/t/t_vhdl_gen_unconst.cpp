// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL unconstrained ports with generics test
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
    for (int data = 0; data < 256; ++data) {
        topp->a = data;
        topp->eval();
        // u1 inverts its 4-bit slice (g_inv false), u2 forwards the whole byte (g_inv true);
        // each instance keeps its own port bounds (o_len) and generic values (o_gen)
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"y1", static_cast<int>(topp->y1), (~data) & 0xf},
                      {"y2", static_cast<int>(topp->y2), data & 0xff},
                      {"len1", static_cast<int>(topp->len1), 4},
                      {"len2", static_cast<int>(topp->len2), 8},
                      {"gen1", static_cast<int>(topp->gen1), 4},
                      {"gen2", static_cast<int>(topp->gen2), 2}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: a=%d: %s=%d expected %d\n", data, check.name, check.got,
                            check.want);
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
