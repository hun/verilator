// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an overload takes a constrained actual for an unconstrained formal
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
    // "abcd" as character codes: the string overload's value
    const unsigned expected = 0x61626364u;
    if (static_cast<unsigned>(topp->o) != expected) {
        std::printf("%%Error: o = %08x, expected %08x from the string overload\n",
                    static_cast<unsigned>(topp->o), expected);
        ++fails;
    }
    if (static_cast<unsigned>(topp->n) != 3u) {
        std::printf("%%Error: n = %u, expected the integer overload's 3\n",
                    static_cast<unsigned>(topp->n));
        ++fails;
    }
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
