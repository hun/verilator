// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL individual record port association test
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
            for (int c = 0; c < 16; ++c) {
                for (int d = 0; d < 16; ++d) {
                    topp->a = a;
                    topp->b = b;
                    topp->c = c;
                    topp->d = d;
                    topp->eval();
                    const int wantUx = a;
                    const int wantUy = b;
                    const int wantCx = b;
                    const int wantCy = c ^ d;
                    const struct {
                        const char* name;
                        int got;
                        int want;
                    } checks[] = {
                        {"ux", static_cast<int>(topp->ux), wantUx},
                        {"uy", static_cast<int>(topp->uy), wantUy},
                        {"cx", static_cast<int>(topp->cx), wantCx},
                        {"cy", static_cast<int>(topp->cy), wantCy},
                    };
                    for (const auto& check : checks) {
                        if (check.got != check.want) {
                            std::printf("%%Error: a=%d b=%d c=%d d=%d %s=%d expected %d\n", a,
                                        b, c, d, check.name, check.got, check.want);
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
