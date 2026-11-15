// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL an instantiation whose entity name is selected
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
    static const int inputs[] = {0x5, 0xa, 0x3};
    for (int value : inputs) {
        topp->d = value;
        topp->eval();
        if (topp->q != value) {
            std::printf("%%Error: d=%x q=%x exp=%x\n", value, topp->q, value);
            ++fails;
        }
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}