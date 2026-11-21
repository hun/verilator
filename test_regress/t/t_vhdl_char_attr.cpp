// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL character-type attributes
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

    topp->code = 97;  // 'a'
    topp->eval();

    int fails = 0;
    struct {
        const char* name;
        uint32_t got;
        uint32_t exp;
    } checks[] = {
        {"c97", topp->c97, 97},  // 'pos(character'val(97))
        {"cupp", topp->cupp, 32},  // 'pos('A') - 'pos('a')
        {"cnxt", topp->cnxt, 98},  // 'pos(succ('a'))
        {"cpvs", topp->cpvs, 96},  // 'pos(pred('a'))
        {"clow", topp->clow, 0},  // 'pos(character'low)
        {"chgh", topp->chgh, 255},  // character'high
        {"clen", topp->clen, 256},  // character'length
    };
    for (const auto& c : checks) {
        if (c.got != c.exp) {
            std::printf("%%Error: %s=%u exp=%u\n", c.name, c.got, c.exp);
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
