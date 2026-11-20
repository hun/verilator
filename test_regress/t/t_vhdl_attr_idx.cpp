// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL positional attributes as index/slice test
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
    // i is (7 downto 0): i'high = 7, i'low = 0.
    const int vectors[4] = {0x96, 0x1F, 0x00, 0xFF};
    for (const int in : vectors) {
        topp->i = static_cast<CData>(in);
        topp->eval();
        topp->eval();  // Let the combinational outputs settle
        const int expHi = (in >> 7) & 1;
        const int expLo = in & 1;
        const int expTop = (in >> 4) & 0xF;
        if (static_cast<int>(topp->o_hi) != expHi) {
            std::printf("%%Error: i=%02x o_hi=%d expected %d\n", in, static_cast<int>(topp->o_hi),
                        expHi);
            ++fails;
        }
        if (static_cast<int>(topp->o_lo) != expLo) {
            std::printf("%%Error: i=%02x o_lo=%d expected %d\n", in, static_cast<int>(topp->o_lo),
                        expLo);
            ++fails;
        }
        if (static_cast<int>(topp->o_full) != in) {
            std::printf("%%Error: i=%02x o_full=%02x expected %02x\n", in,
                        static_cast<int>(topp->o_full), in);
            ++fails;
        }
        if (static_cast<int>(topp->o_top) != expTop) {
            std::printf("%%Error: i=%02x o_top=%x expected %x\n", in,
                        static_cast<int>(topp->o_top), expTop);
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
