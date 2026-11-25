// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL predefined function called where same-named overloads do not take
// the actuals
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

    // The package's to_unsigned, resize and to_int overloads take other actuals, so each
    // call is the predefined one. make_u's call is resolved in its body, inlined here.
    topp->eval();
    int fails = 0;
    const struct {
        const char* name;
        int got;
        int want;
    } checks[] = {{"o_direct", static_cast<int>(topp->o_direct), 3},
                  {"o_inline", static_cast<int>(topp->o_inline), 5},
                  {"o_resize", static_cast<int>(topp->o_resize), 6},
                  {"o_toint", static_cast<int>(topp->o_toint), 3},
                  {"o_bool", static_cast<int>(topp->o_bool), 7}};
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
