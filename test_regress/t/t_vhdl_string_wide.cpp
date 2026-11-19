// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: string constants wider than 32 bits
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
    if (topp->o_len != 32u) {
        std::printf("%%Error: o_len = %u, expected 32 characters\n",
                    static_cast<unsigned>(topp->o_len));
        ++fails;
    }
    // '3' is 0x33 and '!' is 0x21
    if (topp->o_pair != 0x3321u) {
        std::printf("%%Error: o_pair = %04x, expected 3321 from '3' & mark_c\n",
                    static_cast<unsigned>(topp->o_pair));
        ++fails;
    }
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
