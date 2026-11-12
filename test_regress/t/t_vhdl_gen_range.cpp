// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL for-generate over a signal's 'range attribute
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

    topp->d = 0x2;  // 0010
    topp->eval();

    int fails = 0;
    const int q = topp->q;  // each bit of d inverted: 1101
    const int r = topp->r;  // d(3-i): bit-reversed d: 0100
    if (q != 0xD) {
        std::printf("%%Error: q=%x exp=d\n", q);
        ++fails;
    }
    if (r != 0x4) {
        std::printf("%%Error: r=%x exp=4\n", r);
        ++fails;
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
