// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL identifier case folding test
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
    for (int en = 0; en <= 1; ++en) {
        for (int data = 0; data < 16; ++data) {
            // The accessors are the folded names of ports declared I_Data/I_En/O_Data/O_Cnt
            topp->i_data = data;
            topp->i_en = en;
            topp->eval();
            const int want = en ? data : 0;
            const struct {
                const char* name;
                int got;
            } checks[] = {{"o_data", static_cast<int>(topp->o_data)},
                          {"o_cnt", static_cast<int>(topp->o_cnt)}};
            for (const auto& check : checks) {
                if (check.got != want) {
                    std::printf("%%Error: en=%d data=%d: %s=%d expected %d\n", en, data,
                                check.name, check.got, want);
                    ++fails;
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
