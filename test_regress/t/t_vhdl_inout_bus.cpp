// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL inout ports on one resolved signal
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
    const auto check = [&](int en1, int en2, int d1, int d2, int got, int want) {
        if (got != want) {
            std::printf("%%Error: en=%d%d d=%d%d bus=%d expected %d\n", en1, en2, d1, d2, got,
                        want);
            ++fails;
        }
    };

    for (int en1 = 0; en1 < 2; ++en1) {
        for (int en2 = 0; en2 < 2; ++en2) {
            for (int d1 = 0; d1 < 2; ++d1) {
                for (int d2 = 0; d2 < 2; ++d2) {
                    topp->en1 = en1;
                    topp->en2 = en2;
                    topp->d1 = d1;
                    topp->d2 = d2;
                    topp->eval();
                    // The resolved value is the OR of the enabled drivers; an
                    // undriven signal reads 0. Both enabled with different
                    // values is 1 here, where VHDL would produce 'X'.
                    const int want = (en1 ? d1 : 0) | (en2 ? d2 : 0);
                    check(en1, en2, d1, d2, static_cast<int>(topp->y), want * 3);
                }
            }
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
