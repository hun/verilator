// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL real subtype with a range constraint test
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
        // o_bit went through the package function whose return type is the constrained
        // integer subtype; o_above is the folded comparison of the constrained real
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_bit", static_cast<int>(topp->o_bit), bit},
                      {"o_above", static_cast<int>(topp->o_above), 1}};
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
