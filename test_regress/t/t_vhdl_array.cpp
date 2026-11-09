// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL array-of-vectors test
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
        for (int i = 0; i < 4; ++i) {
            topp->a = a;
            topp->i = i;
            topp->eval();

            const int m0 = a;
            const int m1 = (a + 1) & 0xff;
            const int m2 = (a + 2) & 0xff;
            const int m3 = (a + 3) & 0xff;
            const int wantCat = (m3 << 16) | (m2 << 8) | m1;
            struct Check final {
                const char* name;
                int got;
                int want;
            } checks[] = {
                {"y", static_cast<int>(topp->y), m1},
                {"z", static_cast<int>(topp->z), (a + i) & 0xff},
                {"cat", static_cast<int>(topp->cat), wantCat},
                {"agg", static_cast<int>(topp->agg), (a << 8) | a},
                {"sh1", static_cast<int>(topp->sh1), (a + 2) & 0xff},
                {"sh2", static_cast<int>(topp->sh2), (a + 3) & 0xff},
                {"nout", static_cast<int>(topp->nout), (a + 1) & 0xff},
                {"rflag", static_cast<int>(topp->rflag), a & 1},
                {"rdata", static_cast<int>(topp->rdata), (a >> 1) & 1},
                {"d", static_cast<int>(topp->d), (a + 1) & 0xff},
                {"sum", static_cast<int>(topp->sumout), m0 + m1 + m2 + m3},
                {"neg", static_cast<int>(topp->neg) & 0xff, (-a) & 0xff},
                {"slt", static_cast<int>(topp->slt), a >= 128},
                {"len", static_cast<int>(topp->len), 4},
                {"lo", static_cast<int>(topp->lo), 0},
                {"hi", static_cast<int>(topp->hi), 3},
            };
            for (const Check& check : checks) {
                if (check.got != check.want) {
                    std::printf("%%Error: a=%d i=%d %s=%d expected %d\n", a, i, check.name,
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
