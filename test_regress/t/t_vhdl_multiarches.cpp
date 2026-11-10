// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL multiple architectures test
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
    if (topp->y != 1) {
        std::printf("%%Error: y=%d expected 1 (last architecture)\n", static_cast<int>(topp->y));
        topp->final();
        delete topp;
        return 1;
    }

    topp->final();
    delete topp;

    std::printf("*-* All Finished *-*\n");
    return 0;
}
