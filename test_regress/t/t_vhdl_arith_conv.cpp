// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL std_logic_arith conversion builtin test
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
    const struct {
        const char* name;
        int got;
        int want;
    } checks[] = {
        {"u", static_cast<int>(topp->u) & 0xff, 5},
        {"s", static_cast<int>(topp->s) & 0xff, 0xff},
        {"w", static_cast<int>(topp->w) & 0xff, 5},
        {"x", static_cast<int>(topp->x) & 0xff, 0xff},
    };
    for (const auto& check : checks) {
        if (check.got != check.want) {
            std::printf("%%Error: %s=%d expected %d\n", check.name, check.got, check.want);
            ++fails;
        }
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
