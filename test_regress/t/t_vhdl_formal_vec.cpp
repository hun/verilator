// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL unconstrained array formal test
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
    // Both vectors hold (10, 20, 30, 40): one element twice an index into it, the sum
    // over the formal's 'range, three times its 'length, and a 'subtype variable written
    // and read back at the other end of the same bounds
    const struct {
        int up;
        int a;
    } vectors[] = {{1, 40}, {0, 80}};
    for (const auto& vector : vectors) {
        topp->i_up = vector.up;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_a", static_cast<int>(topp->o_a), vector.a},
                      {"o_b", static_cast<int>(topp->o_b), 100},
                      {"o_c", static_cast<int>(topp->o_c), 12},
                      {"o_d", static_cast<int>(topp->o_d), 11}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_up=%d: %s=%d expected %d\n", vector.up, check.name,
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
