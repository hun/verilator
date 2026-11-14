// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL imported package array subtype test
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
    // The two rows of the package's array-of-array constant, and the subtype's length
    // plus one element of a signal declared with that subtype
    const struct {
        int a;
        int first;
        int last;
    } vectors[] = {{1, 1, 5}, {0, 6, 10}};
    for (const auto& vector : vectors) {
        topp->i_a = vector.a;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_first", static_cast<int>(topp->o_first), vector.first},
                      {"o_last", static_cast<int>(topp->o_last), vector.last},
                      {"o_len", static_cast<int>(topp->o_len), 13}};
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
