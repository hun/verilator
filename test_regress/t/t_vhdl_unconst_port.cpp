// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL unconstrained entity port test
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 256; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();
            check("y", static_cast<int>(topp->y), (~a) & 0xf);
            check("v", static_cast<int>(topp->v), (~(b >> 4)) & 0xf);
            check("z", static_cast<int>(topp->z), (~b) & 0xff);
            check("w", static_cast<int>(topp->w), 0xa5);
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
