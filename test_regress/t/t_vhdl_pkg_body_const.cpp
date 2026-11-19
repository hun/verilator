// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: constants of a package body, and non-integer constants built
// by calling functions another package defines in its body
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
    const bool ok = topp->o != 0;
    topp->final();
    delete topp;
    if (!ok) {
        std::printf("%%Error: the constant built in the package body is not '3!' and 0011\n");
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
