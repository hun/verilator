// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL static unsigned vector function test
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
    for (int a = 0; a < 16; ++a) {
        topp->a = a;
        topp->eval();
        const int want = a & 0x7;
        if (static_cast<int>(topp->o) != want) {
            std::printf("%%Error: a=%d o=%d expected %d\n", a, static_cast<int>(topp->o), want);
            ++fails;
        }
        const int wantVi = (a + (a & 1)) & 0xf;
        if (static_cast<int>(topp->vi_o) != wantVi) {
            std::printf("%%Error: a=%d vi_o=%d expected %d\n", a, static_cast<int>(topp->vi_o),
                        wantVi);
            ++fails;
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
