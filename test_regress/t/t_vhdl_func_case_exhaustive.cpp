// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an inlined function with an exhaustive case returns on every path
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
    for (unsigned v = 0; v < 8; ++v) {
        topp->v = v;
        topp->eval();
        const unsigned bits = v & 1;
        const unsigned wantParity = (bits ^ ((v >> 1) & 1) ^ ((v >> 2) & 1)) & 1;
        const unsigned wantEven = (bits == 0) || (((v >> 1) & 1) != 0) ? 1 : 0;
        if (topp->p != wantParity) {
            std::printf("%%Error: v=%u parity=%u expected %u\n", v,
                        static_cast<unsigned>(topp->p), wantParity);
            ++fails;
        }
        if (topp->e != wantEven) {
            std::printf("%%Error: v=%u even=%u expected %u\n", v,
                        static_cast<unsigned>(topp->e), wantEven);
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
