// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL nested interface views on a top entity and on an instance
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
    for (int data = 0; data < 256; data += 51) {
        for (int bits = 0; bits < 4; ++bits) {
            const int valid = bits & 1;
            const int ready = (bits >> 1) & 1;
            const int inv = ~data & 0xff;
            // The top entity's own view port: tx is the master end, rx the slave end
            topp->m__rx__valid = valid;
            topp->m__rx__data = data;
            topp->m__tx__ready = ready;
            topp->a_valid = valid;
            topp->a_data = data;
            topp->b_ready = ready;
            topp->eval();
            if (topp->m__tx__valid != valid || topp->m__tx__data != inv
                || topp->m__rx__ready != ready) {
                std::printf("%%Error: top view port data=%d bits=%d\n", data, bits);
                ++fails;
            }
            // Both instances forward the same beat
            if (topp->b_valid != valid || topp->b_data != inv || topp->a_ready != ready
                || topp->c_valid != valid || topp->c_data != inv || topp->c_ready != ready) {
                std::printf("%%Error: instance data=%d bits=%d\n", data, bits);
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
