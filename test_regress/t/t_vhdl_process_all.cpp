// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL process(all) test
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
    const auto checkAll = [&](int y, int z, int v) {
        check("y", static_cast<int>(topp->y), y);
        check("z", static_cast<int>(topp->z), z);
        check("v", static_cast<int>(topp->v), v);
    };

    topp->a = 0;
    topp->b = 0;
    topp->c = 0;
    topp->sel = 0;
    topp->eval();
    checkAll(0, 0, 0);

    // Input changes retrigger the process without any clock
    topp->a = 1;
    topp->c = 1;
    topp->eval();
    checkAll(0, 1, 0);

    // Only c changes: z depends on it, so the inferred sensitivity must include c
    topp->c = 0;
    topp->eval();
    checkAll(0, 0, 0);
    topp->c = 1;
    topp->eval();
    checkAll(0, 1, 0);

    topp->b = 1;
    topp->eval();
    checkAll(1, 0, 1);

    topp->sel = 1;
    topp->eval();
    checkAll(1, 0, 1);

    topp->a = 0;
    topp->eval();
    checkAll(0, 1, 0);

    if (!fails) std::printf("%%Info: all checks passed\n");
    topp->final();
    delete topp;
    return fails ? 1 : 0;
}
