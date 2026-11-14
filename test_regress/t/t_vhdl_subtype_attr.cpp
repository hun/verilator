// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL 'subtype attribute declaration test
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
    // `shifted` takes a signal's subtype, so its bounds drive both the loop that fills it
    // and its 'length attribute; `element` reads back through a formal's own subtype
    const struct {
        int a;
        int sum;
    } vectors[] = {{1, 72}, {5, 80}};
    for (const auto& vector : vectors) {
        topp->i_a = vector.a;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_sum", static_cast<int>(topp->o_sum), vector.sum},
                      {"o_len", static_cast<int>(topp->o_len), 4}};
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
