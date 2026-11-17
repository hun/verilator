// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL duplicate enumeration literal names
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
    if (static_cast<int>(topp->pos_o) != 20) {
        std::printf("%%Error: pos_o=%d expected 20\n", static_cast<int>(topp->pos_o));
        ++fails;
    }
    if (static_cast<int>(topp->eq_o) != 1) {
        std::printf("%%Error: eq_o=%d expected 1\n", static_cast<int>(topp->eq_o));
        ++fails;
    }
    if (static_cast<int>(topp->case_o) != 1) {
        std::printf("%%Error: case_o=%d expected 1\n", static_cast<int>(topp->case_o));
        ++fails;
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
