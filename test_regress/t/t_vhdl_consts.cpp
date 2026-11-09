// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL constants and generic defaults test
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
    topp->a = 10;
    topp->b = 0xa5;
    topp->eval();
    if (static_cast<int>(topp->n) != 6) {
        std::printf("%%Error: n=%d\n", static_cast<int>(topp->n));
        ++fails;
    }
    if (static_cast<int>(topp->y) != 0xa5) {
        std::printf("%%Error: y=%d\n", static_cast<int>(topp->y));
        ++fails;
    }
    if (static_cast<int>(topp->m0) != 0) {
        std::printf("%%Error: m0=%d\n", static_cast<int>(topp->m0));
        ++fails;
    }
    if (static_cast<int>(topp->mhi) != 3) {
        std::printf("%%Error: mhi=%d\n", static_cast<int>(topp->mhi));
        ++fails;
    }
    if (static_cast<int>(topp->ar0) != 1) {
        std::printf("%%Error: ar0=%d\n", static_cast<int>(topp->ar0));
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
