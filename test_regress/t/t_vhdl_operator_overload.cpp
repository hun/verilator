// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL operator and array-type overload resolution
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

    topp->a0 = 5;
    topp->a1 = 0;
    topp->b0 = 2;
    topp->b1 = 1;
    topp->u = 9;
    topp->s = 9;
    topp->sv = 2;
    topp->eval();

    int fails = 0;
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };
    check("sum0", static_cast<int>(topp->sum0), 107);
    check("sum1", static_cast<int>(topp->sum1), 101);
    check("diff0", static_cast<int>(topp->diff0), 3);
    check("diff1", static_cast<int>(topp->diff1), -1);
    check("neg0", static_cast<int>(topp->neg0), -5);
    check("neg1", static_cast<int>(topp->neg1), -100);
    check("bit_o", static_cast<int>(topp->bit_o), 10);
    check("pick_u", static_cast<int>(topp->pick_u), 1);
    check("pick_s", static_cast<int>(topp->pick_s), 2);
    check("pick_a", static_cast<int>(topp->pick_a), 3);
    check("pick_b", static_cast<int>(topp->pick_b), 4);
    check("pick_f", static_cast<int>(topp->pick_f), 3);
    check("pick_pa", static_cast<int>(topp->pick_pa), 5);
    check("pick_pb", static_cast<int>(topp->pick_pb), 6);
    check("nib_o", static_cast<int>(topp->nib_o), 9);
    check("byte_o", static_cast<int>(topp->byte_o), 0xaa);
    check("nib_eq", static_cast<int>(topp->nib_eq), 1);

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
