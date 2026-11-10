// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL arrays of integer elements test
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
    for (int i = 0; i < 4; ++i) {
        topp->i = i;
        topp->eval();

        struct Check final {
            const char* name;
            int got;
            int want;
        } checks[] = {
            {"q", static_cast<int>(topp->q), i + 1},
            {"r", static_cast<int>(topp->r), 40 - 10 * i},
            {"len", static_cast<int>(topp->len), 4},
        };
        for (const Check& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i=%d %s=%d expected %d\n", i, check.name, check.got,
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
