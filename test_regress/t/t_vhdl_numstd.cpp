// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL numeric_std function test
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
    for (int u = 0; u < 16; ++u) {
        const int sv = u >= 8 ? u - 16 : u;
        for (int i = 0; i < 8; ++i) {
            topp->u = u;
            topp->s = u & 15;
            topp->i = i;
            topp->eval();

            const int n = i % 4;
            const int sl = i >= 4 ? 0 : ((u << i) & 15);
            const int sr = i >= 4 ? 0 : (u >> i);
            const int rl = ((u << n) | (u >> (4 - n))) & 15;
            const int rr = ((u >> n) | (u << (4 - n))) & 15;
            const int ssr = (sv >> i) & 15;
            const int prev = (u - 1) & 15;
            const int minValue = u < prev ? u : prev;
            const int maxValue = u > prev ? u : prev;
            const int sprev = ((sv + 1) & 15);
            const int sprevSigned = sprev >= 8 ? sprev - 16 : sprev;
            const int smin = sv < sprevSigned ? sv : sprevSigned;
            const int smax = sv > sprevSigned ? sv : sprevSigned;

            struct Check final {
                const char* name;
                int got;
                int want;
            } checks[] = {
                {"shift_left", static_cast<int>(topp->sl_o), sl},
                {"shift_right", static_cast<int>(topp->sr_o), sr},
                {"rotate_left", static_cast<int>(topp->rl_o), rl},
                {"rotate_right", static_cast<int>(topp->rr_o), rr},
                {"shift_right_signed", static_cast<int>(topp->ssr_o) & 15, ssr},
                {"minimum", static_cast<int>(topp->min_o), minValue},
                {"maximum", static_cast<int>(topp->max_o), maxValue},
                {"minimum_signed", static_cast<int>(topp->smin_o) & 15, smin & 15},
                {"maximum_signed", static_cast<int>(topp->smax_o) & 15, smax & 15},
            };
            for (const Check& check : checks) {
                if (check.got != check.want) {
                    std::printf("%%Error: u=%d i=%d %s=%d expected %d\n", u, i, check.name,
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
