// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL entity instantiated before its declaration test
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
    // Each generate element and the plain instance must be wired to the child's ports, so
    // `sum` is the xor and `car` the and of the operands, per bit
    for (int xin = 0; xin < 4; ++xin) {
        for (int yin = 0; yin < 4; ++yin) {
            topp->x = xin;
            topp->y = yin;
            topp->eval();
            check("sum", topp->sum, xin ^ yin);
            check("car", topp->car, xin & yin);
            check("s0", static_cast<int>(topp->s0), 1);
            check("c0", static_cast<int>(topp->c0), 0);
        }
    }

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
