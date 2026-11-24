// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL an array element that is itself an array joined to an array
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
    for (unsigned v : {0x123456u, 0xabcdefu, 0x000001u, 0x800000u}) {
        topp->i = v;
        topp->eval();
        // Byte 0 moves to index 1, byte 1 to 2, byte 2 to 0
        const unsigned b0 = v & 0xff;
        const unsigned b1 = (v >> 8) & 0xff;
        const unsigned b2 = (v >> 16) & 0xff;
        const unsigned expect = (b1 << 16) | (b0 << 8) | b2;
        if (topp->o != expect) {
            std::printf("%%Error: i=0x%06x o=0x%06x exp=0x%06x\n", v, topp->o, expect);
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
