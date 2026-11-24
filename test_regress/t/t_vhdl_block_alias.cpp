// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL block_alias test
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
    for (int a = 0; a < 256; a += 7) {
        topp->a = a;
        topp->eval();
        if (topp->hi != (a >> 4) || topp->ext != (~a & 0xff)) {
            std::printf("%%Error: a=%d hi=%d ext=%d\n", a, static_cast<int>(topp->hi),
                        static_cast<int>(topp->ext));
            ++fails;
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
