// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL constant from a pure math_real function test
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
    for (int bit = 0; bit <= 1; ++bit) {
        topp->i_a = bit;
        topp->eval();
        // ceil_log2(8) is 3 and bits_for(5) is ceil_log2(6), which nests the first
        // function; both constants feed the subtype range and the outputs below
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_log", static_cast<int>(topp->o_log), bit ? 3 : 0},
                      {"o_bits", static_cast<int>(topp->o_bits), bit ? 3 : 0},
                      {"o_sum", static_cast<int>(topp->o_sum), bit ? 6 : 0}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_a=%d: %s=%d expected %d\n", bit, check.name, check.got,
                            check.want);
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
