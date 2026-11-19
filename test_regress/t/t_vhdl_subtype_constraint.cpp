// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a discrete subtype as an array index constraint
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
    if (static_cast<unsigned>(topp->a) != 0xau || static_cast<unsigned>(topp->b) != 0x5u
        || static_cast<unsigned>(topp->c) != 0x3u) {
        std::printf("%%Error: a=%x b=%x c=%x expected a/5/3\n", static_cast<unsigned>(topp->a),
                    static_cast<unsigned>(topp->b), static_cast<unsigned>(topp->c));
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
