// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL subtype range naming a constant that a function completes
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
    // BITS_OF counts the bits of 7 through a loop, so the architecture's SMALL_T is only
    // known to be 3 wide once that body has run: the width shows in the truncation of the
    // source down to the subtype, and in the bit the generate's grown copy can reach. The
    // entity's own constant completes through TRIPLED's body, which gives WIDE_T 9 bits.
    const struct {
        int delta;
        int wrap;
    } vectors[] = {{0, 1}, {4, 5}};
    for (const auto& vector : vectors) {
        topp->i_delta = vector.delta;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_len", static_cast<int>(topp->o_len), 3},
                      {"o_wrap", static_cast<int>(topp->o_wrap), vector.wrap},
                      {"o_wlen", static_cast<int>(topp->o_wlen), 9},
                      {"o_wide", static_cast<int>(topp->o_wide), 61},
                      {"o_gen", static_cast<int>(topp->o_gen), 0}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_delta=%d: %s=%d expected %d\n", vector.delta, check.name,
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
