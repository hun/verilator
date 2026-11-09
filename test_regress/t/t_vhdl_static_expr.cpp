// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL static ranges, aggregates and function locals
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
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 16; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();

            struct Check final {
                const char* name;
                int got;
                int want;
            } checks[] = {
                {"o_exp", static_cast<int>(topp->o_exp), a},
                {"o_div", static_cast<int>(topp->o_div), a},
                {"o_qual", static_cast<int>(topp->o_qual), (a + (b & 1)) & 0xf},
                {"o_func", static_cast<int>(topp->o_func), 0xc & ~a},
                {"o_alt", static_cast<int>(topp->o_alt), 0xc ^ a},
                {"o_pos", static_cast<int>(topp->o_pos), 0x6 ^ a},
                {"o_rng", static_cast<int>(topp->o_rng), a},
                {"o_child", static_cast<int>(topp->o_child), a & 1},
            };
            for (const Check& check : checks) {
                if (check.got != check.want) {
                    std::printf("%%Error: a=%d b=%d %s=%d expected %d\n", a, b, check.name,
                                check.got, check.want);
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
