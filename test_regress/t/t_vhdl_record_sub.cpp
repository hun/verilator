// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL constrained record subtype test
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
    for (int a4 = 0; a4 < 16; ++a4) {
        for (int b8 = 0; b8 < 256; ++b8) {
            topp->a4 = a4;
            topp->b8 = b8;
            topp->eval();
            const struct {
                const char* name;
                int got;
                int want;
            } checks[] = {
                {"oax", static_cast<int>(topp->oax), a4},
                {"oay", static_cast<int>(topp->oay), b8},
                {"obx", static_cast<int>(topp->obx), a4},
                {"oby", static_cast<int>(topp->oby), b8},
                {"ocx", static_cast<int>(topp->ocx), b8},
                {"ocy", static_cast<int>(topp->ocy), a4},
                {"co1", static_cast<int>(topp->co1), a4},
                {"co2", static_cast<int>(topp->co2), b8 & 0xf},
            };
            for (const auto& check : checks) {
                if (check.got != check.want) {
                    std::printf("%%Error: a4=%d b8=%d %s=%d expected %d\n", a4, b8, check.name,
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
