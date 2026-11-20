// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: type bounds that name an object or a folded constant
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
        topp->din = v;
        topp->eval();
        const unsigned pair = (v << 4) | v;
        const unsigned rotated = (pair >> 1) | ((pair & 1u) << 7);
        const unsigned want = (~v & 0xf) ^ ((pair ^ rotated) & 0xf);
        if (static_cast<unsigned>(topp->dout) != want) {
            std::printf("%%Error: din=%x dout=%x expected %x\n", v,
                        static_cast<unsigned>(topp->dout), want);
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
