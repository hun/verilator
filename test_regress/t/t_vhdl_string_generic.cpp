// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: string generics selecting generate branches
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
    for (unsigned v = 0; v < 16; ++v) {
        topp->i = v;
        topp->eval();
        const unsigned pass = static_cast<unsigned>(topp->o_pass);
        const unsigned inv = static_cast<unsigned>(topp->o_inv);
        const unsigned def = static_cast<unsigned>(topp->o_def);
        if (pass != v || def != v || inv != ((~v) & 0xfu)) {
            std::printf("%%Error: i=%x pass=%x inv=%x def=%x\n", v, pass, inv, def);
            ++fails;
        }
        if (topp->e_pass != 1 || topp->e_inv != 0) {
            std::printf("%%Error: i=%x empty flags %d %d expected 1 0\n", v,
                        static_cast<int>(topp->e_pass), static_cast<int>(topp->e_inv));
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
