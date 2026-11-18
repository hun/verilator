// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a real generic
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
    // 50/4 = 12.5 -> 1250; min(1250, 1000) = 1000; pick(1250, 2000) = 1250
    if (topp->n1 != 1250 || topp->m1 != 2250) {
        std::printf("%%Error: n1=%d m1=%d expected 1250/2250\n", static_cast<int>(topp->n1),
                    static_cast<int>(topp->m1));
        ++fails;
    }
    // 100/3 = 33.333 -> 3333; min(3333, 1000) = 1000; pick(3333, 2000) = 2000
    if (topp->n2 != 3333 || topp->m2 != 3000) {
        std::printf("%%Error: n2=%d m2=%d expected 3333/3000\n", static_cast<int>(topp->n2),
                    static_cast<int>(topp->m2));
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
