// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL component declaration and component instantiation
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

    // The component is declared in comp_pkg and the architecture names only the component,
    // so the entity is bound by name. The `component NAME` spelling instantiates the same
    // entity with its default generic. Both add three and five, like the entity
    // instantiation next to them, so every output reads 8 if the right entity ran.
    topp->eval();
    int fails = 0;
    const struct {
        const char* name;
        int got;
        int want;
    } checks[] = {{"o_comp", static_cast<int>(topp->o_comp), 8},
                  {"o_kw", static_cast<int>(topp->o_kw), 8},
                  {"o_ent", static_cast<int>(topp->o_ent), 8}};
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