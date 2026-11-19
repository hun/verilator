// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a declaration hides an imported one of the same profile
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
    // child's tag returns 2; base's, which must stay hidden, returns 1
    if (topp->o_spec != 2u) {
        std::printf("%%Error: o_spec = %u, expected 2 from child's tag\n", topp->o_spec);
        ++fails;
    }
    if (topp->o_body != 2u) {
        std::printf("%%Error: o_body = %u, expected 2 from the body's tag\n", topp->o_body);
        ++fails;
    }
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
