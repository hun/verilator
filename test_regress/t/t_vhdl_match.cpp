// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL match operator test
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
    const auto checkAll = [&](int eq, int ne, int mid, int ab) {
        check("z_eq", static_cast<int>(topp->z_eq), eq);
        check("z_ne", static_cast<int>(topp->z_ne), ne);
        check("z_mid", static_cast<int>(topp->z_mid), mid);
        check("z_ab", static_cast<int>(topp->z_ab), ab);
    };

    topp->a = 0;
    topp->b = 0;
    topp->eval();
    checkAll(0, 1, 0, 1);

    // Bits 3 and 0 compare, bits 2 and 1 are don't-care
    topp->a = 8;
    topp->b = 8;
    topp->eval();
    checkAll(1, 0, 0, 1);

    topp->a = 9;
    topp->eval();
    checkAll(0, 1, 1, 0);

    topp->a = 12;
    topp->eval();
    checkAll(1, 0, 0, 0);

    topp->a = 4;
    topp->eval();
    checkAll(0, 1, 0, 0);

    topp->b = 4;
    topp->eval();
    checkAll(0, 1, 0, 1);

    if (!fails) std::printf("%%Info: all checks passed\n");
    topp->final();
    delete topp;
    return fails ? 1 : 0;
}
