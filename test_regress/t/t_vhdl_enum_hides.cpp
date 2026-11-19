// -*- mode: C++; c-file-style: "cc-mode" //
//
// DESCRIPTION: Verilator: a declaration hides an enumeration literal of a used package
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

    int fails = 0;
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };
    const auto drive = [&](int ra) {
        topp->ra = ra;
        topp->eval();
    };

    // `ra` drives o_sel. `lit` is a_t'low, a_t's `ra` at position 0; b_t also
    // declares `ra` at position 1, so taking that literal would make lit equal
    // a_t's rbb and raise o_lit.
    drive(0);
    check("o_sel", static_cast<int>(topp->o_sel), 0);
    check("o_lit", static_cast<int>(topp->o_lit), 0);
    drive(1);
    check("o_sel", static_cast<int>(topp->o_sel), 1);
    check("o_lit", static_cast<int>(topp->o_lit), 0);
    drive(0);
    check("o_sel", static_cast<int>(topp->o_sel), 0);
    check("o_lit", static_cast<int>(topp->o_lit), 0);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
