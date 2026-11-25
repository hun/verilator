// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL interface view ports on the top entity
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
    for (int beat = 0; beat < 256; beat += 51) {
        for (int ready = 0; ready < 2; ++ready) {
            topp->s_axis__valid = beat & 1;
            topp->s_axis__data = beat;
            topp->m_axis__ready = ready;
            topp->eval();
            // m_axis is the master end, so it drives valid and data and reads ready
            if (topp->m_axis__valid != (beat & 1) || topp->m_axis__data != ((~beat) & 0xff)
                || topp->s_axis__ready != ready) {
                std::printf("%%Error: beat=%d ready=%d valid=%d data=%d sready=%d\n", beat, ready,
                            topp->m_axis__valid, topp->m_axis__data, topp->s_axis__ready);
                ++fails;
            }
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
