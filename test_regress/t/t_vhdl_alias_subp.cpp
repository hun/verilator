// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL subprogram alias test
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    // Each alias renames one of package p's functions: i_a + 5 through the two-argument
    // one (5 being the constant folded through an architecture-level alias), 2 * i_a
    // through the one-formal overload of an overloaded designator, i_a + 10 through an
    // alias published by another package, and 2 * (i_a + 1) and i_a + 3 through aliases
    // declared in a subprogram's own declarative part
    const struct {
        int a;
        int sum;
        int dbl;
        int q;
        int inside;
        int nested;
    } vectors[] = {{7, 12, 14, 17, 16, 10}, {0, 5, 0, 10, 2, 3}};
    for (const auto& vector : vectors) {
        topp->i_a = vector.a;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_sum", static_cast<int>(topp->o_sum), vector.sum},
                      {"o_dbl", static_cast<int>(topp->o_dbl), vector.dbl},
                      {"o_q", static_cast<int>(topp->o_q), vector.q},
                      {"o_const", static_cast<int>(topp->o_const), 5},
                      {"o_inside", static_cast<int>(topp->o_inside), vector.inside},
                      {"o_nested", static_cast<int>(topp->o_nested), vector.nested}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_a=%d: %s=%d expected %d\n", vector.a, check.name,
                            check.got, check.want);
                ++fails;
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
