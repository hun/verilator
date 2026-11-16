// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a constant expression with real operands folds at elaboration
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>
#include <cstdint>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    topp->eval();
    int fails = 0;
    const auto check = [&](const char* name, uint32_t got, uint32_t want) {
        if (got != want) {
            std::printf("%%Error: %s=%u expected %u\n", name, got, want);
            ++fails;
        }
    };
    // 1e9/125/1e8*268435456 = 21474836.48, rounded; 2.7 rounds to 3
    check("o_div", topp->o_div, 21474836u);
    check("o_rnd", topp->o_rnd, 3u);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}