// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a type attribute of a variable assigned from a call
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
    for (unsigned i = 0; i < 256; ++i) {
        topp->i = i;
        topp->eval();
        // The function walks `v'range`, i.e. downward, accumulating an OR suffix, and
        // the grant isolates its highest set bit
        unsigned acc = 0;
        unsigned run = 0;
        for (int b = 7; b >= 0; --b) {
            run |= (i >> b) & 1u;
            if (run) acc |= 1u << b;
        }
        const unsigned want = acc & ~(acc >> 1) & 0xffu;
        if (static_cast<unsigned>(topp->o) != want) {
            if (fails < 8) {
                std::printf("%%Error: i=%02x o=%02x expected %02x\n", i,
                            static_cast<unsigned>(topp->o), want);
            }
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
