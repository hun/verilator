// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL call bound to the overload whose formal takes the actual
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

    // Each name has two declarations taking one actual, and the one whose formal cannot
    // take the actual comes first. PICK's array body adds the actual's two elements, its
    // integer body adds a hundred, so the value says which ran; the integer actual is a
    // folded constant once and a signal once, which are classified from different tables.
    // FLAG's natural body adds ten to its actual, its boolean body answers 5 for true. The
    // generate and loop ranges fold the call, as a static expression has nothing to lower.
    topp->eval();
    int fails = 0;
    const struct {
        const char* name;
        int got;
        int want;
    } checks[] = {{"o_vec", static_cast<int>(topp->o_vec), 52},
                  {"o_const", static_cast<int>(topp->o_const), 112},
                  {"o_var", static_cast<int>(topp->o_var), 107},
                  {"o_nat", static_cast<int>(topp->o_nat), 14},
                  {"o_bool", static_cast<int>(topp->o_bool), 5},
                  {"o_fold", static_cast<int>(topp->o_fold), 215},
                  {"o_bits", static_cast<int>(topp->o_bits), 7},
                  {"o_sum", static_cast<int>(topp->o_sum), 12},
                  {"o_var2", static_cast<int>(topp->o_var2), 14},
                  {"o_lit2", static_cast<int>(topp->o_lit2), 6}};
    for (const auto& check : checks) {
        if (check.got != check.want) {
            std::printf("%%Error: %s=%d expected %d\n", check.name, check.got, check.want);
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
