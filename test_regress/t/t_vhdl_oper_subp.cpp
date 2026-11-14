// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL subprogram whose name is an operator symbol test
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
    for (int bit_a = 0; bit_a <= 1; ++bit_a) {
        for (int bit_b = 0; bit_b <= 1; ++bit_b) {
            topp->i_a = bit_a;
            topp->i_b = bit_b;
            topp->eval();
            // o_ad came from "and"(boolean, std_logic), o_or from "or"(std_logic,
            // boolean), so each value shows which body ran with which argument order
            const struct {
                const char* name;
                int got;
                int want;
            } checks[] = {{"o_ad", static_cast<int>(topp->o_ad), bit_a && bit_b},
                          {"o_or", static_cast<int>(topp->o_or), bit_a || bit_b}};
            for (const auto& check : checks) {
                if (check.got != check.want) {
                    std::printf("%%Error: i_a=%d i_b=%d: %s=%d expected %d\n", bit_a, bit_b,
                                check.name, check.got, check.want);
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
