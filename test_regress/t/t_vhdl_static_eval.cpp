// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL static function evaluation test
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
    for (int a = 0; a < 256; ++a) {
        topp->a = a;
        topp->eval();

        struct Check final {
            const char* name;
            int got;
            int want;
        } checks[] = {
            {"y", static_cast<int>(topp->y), a & 0xf},
            {"z", static_cast<int>(topp->z), a & 0x1f},
            {"hi", static_cast<int>(topp->hi), 7},
            {"sel", static_cast<int>(topp->sel), (a & 3) == 1},
            {"strout", static_cast<int>(topp->strout), 1},
        };
        for (const Check& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: a=%d %s=%d expected %d\n", a, check.name, check.got,
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
