// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL physical (time) literals and the time type
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
    topp->eval();

    int fails = 0;
    const int got[6] = {static_cast<int>(topp->q_ratio), static_cast<int>(topp->q_half),
                        static_cast<int>(topp->q_big),   static_cast<int>(topp->q_frac),
                        static_cast<int>(topp->q_min),   static_cast<int>(topp->q_hr)};
    const int exp[6] = {25, 30, 1000, 333, 60, 60};
    const char* const name[6]
        = {"q_ratio", "q_half", "q_big",
           "q_frac",  "q_min",  "q_hr"};  // 200ns/8ns, 60ns/2/1ns, 1s/1ms, us/ns, min/s
    for (int i = 0; i < 6; ++i) {
        if (got[i] != exp[i]) {
            std::printf("%%Error: %s=%d exp=%d\n", name[i], got[i], exp[i]);
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
