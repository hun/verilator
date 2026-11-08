// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL object aliases test
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
    for (int din = 0; din < 2; ++din) {
        topp->din = din;
        topp->eval();
        if (static_cast<int>(topp->y) != 1) {
            std::printf("%%Error: din=%d y=%d expected 1\n", din, static_cast<int>(topp->y));
            ++fails;
        }
        if (static_cast<int>(topp->hi_o) != (din << 1)) {
            std::printf("%%Error: din=%d hi_o=%d expected %d\n", din,
                        static_cast<int>(topp->hi_o), din << 1);
            ++fails;
        }
        if (static_cast<int>(topp->n) != 2) {
            std::printf("%%Error: n=%d expected 2\n", static_cast<int>(topp->n));
            ++fails;
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
