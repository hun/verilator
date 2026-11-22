// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an aggregate operand keeps the bounds its choices name
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
    int fails = 0;
    // The generic is (1, 3, 5), so the largest tap is 5
    if (topp->o_max != 5) {
        std::printf("%%Error: o_max = %u, expected 5\n", static_cast<unsigned>(topp->o_max));
        ++fails;
    }
    // The one-element aggregate adds the carry bit, not a zero-extended vector
    const unsigned cases[][3]
        = {{0x00, 0x00, 0}, {0x0d, 0x07, 1}, {0xff, 0x01, 0}, {0x80, 0x80, 1}, {0x55, 0xaa, 0}};
    for (const auto& vec : cases) {
        topp->a = vec[0];
        topp->b = vec[1];
        topp->c = vec[2];
        topp->eval();
        const unsigned want = (vec[0] + vec[1] + vec[2]) & 0xffu;
        const unsigned got = static_cast<unsigned>(topp->s);
        if (got != want) {
            std::printf("%%Error: %02x+%02x+%u = %02x, expected %02x\n", vec[0], vec[1], vec[2],
                        got, want);
            ++fails;
        }
    }
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
