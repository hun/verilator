// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: unconstrained record port element association test
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
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 16; ++b) {
            for (int w = 0; w < 16; ++w) {
                for (int c = 0; c < 8; ++c) {
                    for (int d = 0; d < 4; ++d) {
                        for (int f = 0; f < 2; ++f) {
                            topp->a = a;
                            topp->b = b;
                            topp->w = w;
                            topp->c = c;
                            topp->d = d;
                            topp->f = f;
                            topp->eval();
                            const int wantMo = f ? (w ^ a) : w;
                            const struct {
                                const char* name;
                                int got;
                                int want;
                            } checks[] = {
                                {"ux", static_cast<int>(topp->ux), a},
                                {"uy", static_cast<int>(topp->uy), b},
                                {"vx", static_cast<int>(topp->vx), c},
                                {"vy", static_cast<int>(topp->vy), d},
                                {"mo", static_cast<int>(topp->mo), wantMo},
                            };
                            for (const auto& check : checks) {
                                if (check.got != check.want) {
                                    std::printf("%%Error: a=%d b=%d w=%d c=%d d=%d f=%d %s=%d"
                                                " expected %d\n",
                                                a, b, w, c, d, f, check.name, check.got,
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

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
