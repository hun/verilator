// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an arithmetic result assigned to a std_logic_vector
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
    const struct {
        unsigned a;
        unsigned b;  // Three bits; as signed it is b - 8 when bit 2 is set
    } cases[] = {{9, 3}, {15, 7}, {6, 5}, {0, 4}, {13, 1}};
    for (const auto& c : cases) {
        topp->a = c.a;
        topp->b = c.b;
        topp->eval();
        const int bs = (c.b & 4) ? static_cast<int>(c.b) - 8 : static_cast<int>(c.b);
        const unsigned sum = (c.a + c.b) & 0xF;
        const unsigned prod = (c.a * c.b) & 0x7F;
        const unsigned mixed = static_cast<unsigned>(static_cast<int>(c.a) * bs) & 0x7F;
        const int as = (c.a & 8) ? static_cast<int>(c.a) - 16 : static_cast<int>(c.a);
        const unsigned neg = static_cast<unsigned>(-as) & 0xF;
        const unsigned magnitude = static_cast<unsigned>(as < 0 ? -as : as) & 0xF;
        if (topp->sum != sum || topp->prod != prod || topp->mixed != mixed || topp->plus != c.a
            || topp->neg != neg || topp->magnitude != magnitude) {
            std::printf("%%Error: a=%u b=%u sum=%x prod=%x mixed=%x exp %x %x %x\n", c.a, c.b,
                        static_cast<unsigned>(topp->sum), static_cast<unsigned>(topp->prod),
                        static_cast<unsigned>(topp->mixed), sum, prod, mixed);
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
