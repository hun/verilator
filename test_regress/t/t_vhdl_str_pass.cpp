// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL string generic passed on to a sub-instance
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
    const auto checkInt = [&](const char* what, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d exp=%d\n", what, got, want);
            ++fails;
        }
    };
    // u0 binds "abc" through mid, so the leaf sees three characters and a match
    checkInt("len0", topp->len0, 3);
    checkInt("eq0", topp->eq0, 1);
    // u1 binds "xyzw", so the leaf sees four characters and no match
    checkInt("len1", topp->len1, 4);
    checkInt("eq1", topp->eq1, 0);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
