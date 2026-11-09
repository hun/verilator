// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL user-defined function calls test
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
    for (int a = -3; a <= 3; ++a) {
        for (int b = -3; b <= 3; ++b) {
            for (int s = 0; s < 3; ++s) {
                for (int u = 0; u < 16; ++u) {
                    for (int st = 0; st < 3; ++st) {
                        for (int en = 0; en < 2; ++en) {
                            for (int num = 0; num < 4; ++num) {
                                topp->a = a;
                                topp->b = b;
                                topp->s = s;
                                topp->u = u;
                                topp->st_in = st;
                                topp->din_en = en;
                                topp->din_num = num;
                                topp->dv = u;
                                topp->eval();

                                const int pick = s == 0 ? 9 : s == 1 ? 7 : 63;
                                const int maxValue = a > b ? a : b;
                                const int y = (a + 1) + maxValue + pick + 2 * b;
                                const int clampValue = u > 8 ? 8 : u;
                                const int w = 1 + (((a + 1) > b) ? (a + 1) : b);
                                const int nextState = (st + 1) % 3;
                                struct Check final {
                                    const char* name;
                                    int got;
                                    int want;
                                } checks[] = {
                                    {"y", static_cast<int>(topp->y), y},
                                    {"v", static_cast<int>(topp->v), clampValue},
                                    {"w", static_cast<int>(topp->w), w},
                                    {"nn", static_cast<int>(topp->nn), nextState},
                                    {"r_en", static_cast<int>(topp->r_en), en},
                                    {"r_st", static_cast<int>(topp->r_st), 0},
                                    {"r_num", static_cast<int>(topp->r_num), num},
                                    {"mk", static_cast<int>(topp->mk), u & 1},
                                };
                                for (const Check& check : checks) {
                                    if (check.got != check.want) {
                                        std::printf("%%Error: a=%d b=%d s=%d u=%d st=%d en=%d "
                                                    "num=%d %s=%d expected %d\n",
                                                    a, b, s, u, st, en, num, check.name, check.got,
                                                    check.want);
                                        ++fails;
                                    }
                                }
                            }
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
