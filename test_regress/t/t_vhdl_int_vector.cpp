// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: the predefined integer_vector
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
    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    topp->clk = 0;
    topp->eval();
    int fails = 0;
    // width 32 > 16, so the plan is (0, 32 / 8 = 4): no URAM, and the BRAM range covers
    // bits 0 .. 4 * 4 - 1 = 15, which is 16 bits
    if (topp->o_uram != 0u) {
        std::printf("%%Error: o_uram = %u, expected 0 URAM blocks\n",
                    static_cast<unsigned>(topp->o_uram));
        ++fails;
    }
    if (topp->o_bram != 16u) {
        std::printf("%%Error: o_bram = %u, expected 16 bits of BRAM\n",
                    static_cast<unsigned>(topp->o_bram));
        ++fails;
    }
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
