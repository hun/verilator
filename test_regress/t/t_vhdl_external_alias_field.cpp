// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL external_alias_field test
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
    for (int a = 0; a < 2; ++a) {
        topp->a = a;
        topp->eval();
        // x(1).r is not a: a = 0 selects x(0).d, a = 1 selects x(1).d
        const int want = a ? 0xa5 : 0x5a;
        if (topp->res != want || topp->b != a) {
            std::printf("%%Error: a=%d res=%x b=%d\n", a, static_cast<int>(topp->res),
                        static_cast<int>(topp->b));
            ++fails;
        }
    }

    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
