// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL attributes of the predefined integer types
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
    // The package body completes width_c by calling the formal-less function, whose body
    // reads POSITIVE'LOW and takes log2 of the converted real: log2(1024) is 10, so the
    // width is 11. The bounds of INTEGER and NATURAL are those of the 32-bit signed model,
    // and all three types ascend, so RIGHT is the high bound and LEFT the low one.
    const struct {
        int delta;
        int high;
    } vectors[] = {{7, 2147483640}, {3, 2147483644}};
    for (const auto& vector : vectors) {
        topp->i_delta = vector.delta;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_bits", static_cast<int>(topp->o_bits), 11},
                      {"o_high", static_cast<int>(topp->o_high), vector.high},
                      {"o_low", static_cast<int>(topp->o_low), -2147483647 - 1},
                      {"o_nat", static_cast<int>(topp->o_nat), 0},
                      {"o_right", static_cast<int>(topp->o_right), 2147483647},
                      {"o_nat_l", static_cast<int>(topp->o_nat_l), 0}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_delta=%d: %s=%d expected %d\n", vector.delta, check.name,
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
