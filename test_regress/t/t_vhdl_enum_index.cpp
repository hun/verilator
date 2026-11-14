// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an array indexed by an enumeration type
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };
    // The enum literals are positions 0, 1 and 2, so v(aa) is the first element and
    // v(cc) the third
    check("o_a", static_cast<int>(topp->o_a), 1);
    check("o_c", static_cast<int>(topp->o_c), 1);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}