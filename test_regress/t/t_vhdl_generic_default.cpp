// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an input port default that names the entity's own generics
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

namespace {
int check(const char* what, unsigned got, unsigned expected) {
    if (got == expected) return 0;
    std::printf("%%Error: %s = %u, expected %u\n", what, got, expected);
    return 1;
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;
    topp->clk = 0;
    topp->eval();
    int fails = 0;
    for (unsigned v = 0; v < 8; ++v) {
        topp->d_a = v;
        topp->d_b = 0x8u | v;
        topp->eval();
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
        // Instance a uses the defaults: five is two bits wide above zero, so the
        // configuration is four in three bits. Instance b binds period nine and a width
        // of four, so the default it applies is eight in four bits.
        fails += check("cfg_a", static_cast<unsigned>(topp->cfg_a), 4u);
        fails += check("cfg_b", static_cast<unsigned>(topp->cfg_b), 8u);
        fails += check("q_a", static_cast<unsigned>(topp->q_a), v);
        fails += check("q_b", static_cast<unsigned>(topp->q_b), 0x8u | v);
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
