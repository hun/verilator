// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL individual slice port association test
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
    for (int i0 = 0; i0 < 16; ++i0) {
        for (int i1 = 0; i1 < 16; ++i1) {
            for (int i2 = 0; i2 < 16; ++i2) {
                for (int i3 = 0; i3 < 16; ++i3) {
                    topp->i0 = i0;
                    topp->i1 = i1;
                    topp->i2 = i2;
                    topp->i3 = i3;
                    topp->eval();
                    const struct {
                        const char* name;
                        int got;
                        int want;
                    } checks[] = {
                        {"o0", static_cast<int>(topp->o0), i0 ^ i2},
                        {"o1", static_cast<int>(topp->o1), i1 ^ i3},
                        {"o2", static_cast<int>(topp->o2), i0 & i2},
                        {"o3", static_cast<int>(topp->o3), i1 & i3},
                    };
                    for (const auto& check : checks) {
                        if (check.got != check.want) {
                            std::printf("%%Error: i0=%d i1=%d i2=%d i3=%d %s=%d expected %d\n",
                                        i0, i1, i2, i3, check.name, check.got, check.want);
                            ++fails;
                        }
                    }
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
