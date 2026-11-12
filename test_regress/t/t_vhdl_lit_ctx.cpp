// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL character/string literal context
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

    // ch selects a character code; o(0)=sa<sb=1, o(1)=c>='a', o(2)=c<='z', o(3)=sa="ab"=1
    const CData chVal[2] = {0x61, 0x37};  // 'a' and '7'
    const CData oExp[2] = {0xF, 0xD};

    int fails = 0;
    for (int i = 0; i < 2; ++i) {
        topp->ch = chVal[i];
        topp->eval();
        const int got = topp->o;
        if (got != oExp[i]) {
            std::printf("%%Error: ch=%x o=%x exp=%x\n", chVal[i], got, oExp[i]);
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
