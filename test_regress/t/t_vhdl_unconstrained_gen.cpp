// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an unconstrained vector constant and generic take their width
//              from the literal that defaults them
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
    // 00111 and 00101, both five bits wide
    const int got = static_cast<int>(topp->o);
    topp->final();
    delete topp;

    if (got != 0x5) {
        std::printf("%%Error: o=%x expected 5\n", got);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}