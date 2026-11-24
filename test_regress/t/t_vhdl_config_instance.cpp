// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an instance of a configuration
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
    for (unsigned in = 0; in < 2; ++in) {
        topp->i = in;
        topp->eval();
        // buf copies, inv inverts, and an instance naming none gets the last architecture
        if (topp->kept != in || topp->inverted != !in || topp->last != 1) {
            std::printf("%%Error: i=%u kept=%d inverted=%d last=%d\n", in,
                        static_cast<int>(topp->kept), static_cast<int>(topp->inverted),
                        static_cast<int>(topp->last));
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
