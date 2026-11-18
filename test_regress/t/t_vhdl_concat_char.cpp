// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a character literal in a concatenation is one logic element
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
    const auto check = [&](unsigned v, unsigned x) {
        topp->v = v;
        topp->x = x;
        topp->eval();
        const unsigned wantLo = v ^ x;
        const unsigned wantHi = ((v << 1) | 1) & 0x1ff;
        if (topp->lo != wantLo) {
            std::printf("%%Error: v=%02x x=%03x lo=%03x expected %03x\n", v, x,
                        static_cast<unsigned>(topp->lo), wantLo);
            ++fails;
        }
        if (topp->hi != wantHi) {
            std::printf("%%Error: v=%02x hi=%03x expected %03x\n", v,
                        static_cast<unsigned>(topp->hi), wantHi);
            ++fails;
        }
    };

    // A character code would put 0x30 or 0x31 in the low bits instead of one bit
    check(0x00, 0x000);
    check(0xff, 0x1ff);
    check(0x7f, 0x0aa);
    check(0x80, 0x155);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
