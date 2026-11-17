// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL std_logic_arith conversion builtin test
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

    topp->eval();

    int fails = 0;
    static const int inV[4] = {0x01, 0x81, 0x5a, 0xff};
    for (const int v : inV) {
        for (int n = 0; n <= 9; ++n) {
            topp->v = v;
            topp->n = n;
            topp->eval();
            const int shl = n >= 8 ? 0 : (v << n) & 0xff;
            const int shr = n >= 8 ? 0 : (v >> n) & 0xff;
            if ((static_cast<int>(topp->sl) & 0xff) != shl
                || (static_cast<int>(topp->sr) & 0xff) != shr) {
                std::printf("%%Error: v=%02x n=%d sl=%02x sr=%02x exp %02x %02x\n", v, n,
                            static_cast<int>(topp->sl) & 0xff,
                            static_cast<int>(topp->sr) & 0xff, shl, shr);
                ++fails;
            }
        }
    }
    topp->v = 0;
    topp->n = 0;
    topp->eval();
    const struct {
        const char* name;
        int got;
        int want;
    } checks[] = {
        {"u", static_cast<int>(topp->u) & 0xff, 5},
        {"s", static_cast<int>(topp->s) & 0xff, 0xff},
        {"w", static_cast<int>(topp->w) & 0xff, 5},
        {"x", static_cast<int>(topp->x) & 0xff, 0xff},
    };
    for (const auto& check : checks) {
        if (check.got != check.want) {
            std::printf("%%Error: %s=%d expected %d\n", check.name, check.got, check.want);
            ++fails;
        }
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
