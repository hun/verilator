// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL folded function body test
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
    // The four constants come from bodies the folding has to run: first_length returns
    // from inside nested loops, scaled from the case arm its argument decides, and
    // dyn_max's result only depends on i_a, so its body is built as a run-time expression
    const struct {
        int a;
        int dyn;
    } vectors[] = {{2, 2}, {0, 0}, {99, 3}};
    for (const auto& vector : vectors) {
        topp->i_a = vector.a;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_first1", static_cast<int>(topp->o_first1), 3},
                      {"o_first2", static_cast<int>(topp->o_first2), 2},
                      {"o_scaled3", static_cast<int>(topp->o_scaled3), 30},
                      {"o_scaled2", static_cast<int>(topp->o_scaled2), 20},
                      {"o_dyn", static_cast<int>(topp->o_dyn), vector.dyn}};
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
