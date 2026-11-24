// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: signed operations on a converted std_logic_vector
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
    for (int raw = 0; raw < 16; ++raw) {
        topp->a = raw;
        topp->eval();
        const int v = (raw & 8) ? raw - 16 : raw;
        const unsigned negative = v < 0;
        const unsigned above = v >= -2;
        const unsigned shifted = static_cast<unsigned>(v >> 1) & 0xF;
        const unsigned extended = static_cast<unsigned>(v) & 0x3F;
        const unsigned halved = static_cast<unsigned>(v / 2) & 0xF;
        const unsigned magnitude = static_cast<unsigned>(v < 0 ? -v : v) & 0xF;
        if (topp->negative != negative || topp->above != above || topp->shifted != shifted
            || topp->extended != extended || topp->halved != halved
            || topp->magnitude != magnitude) {
            std::printf("%%Error: a=%d neg=%d above=%d shift=%x ext=%x half=%x mag=%x\n", v,
                        static_cast<int>(topp->negative), static_cast<int>(topp->above),
                        static_cast<unsigned>(topp->shifted),
                        static_cast<unsigned>(topp->extended), static_cast<unsigned>(topp->halved),
                        static_cast<unsigned>(topp->magnitude));
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
