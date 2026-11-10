// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL level-gated clock wait test
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
    const auto tick = [&]() {
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
    };

    topp->en = 0;
    topp->clk = 0;
    topp->eval();
    for (int i = 0; i < 3; ++i) tick();
    check("once before enable", static_cast<int>(topp->once_o), 0);
    check("count before enable", static_cast<int>(topp->count_o), 0);

    topp->en = 1;
    for (int i = 1; i <= 4; ++i) {
        tick();
        check("count with enable", static_cast<int>(topp->count_o), i);
        check("once after first edge", static_cast<int>(topp->once_o), 7);
    }

    topp->en = 0;
    for (int i = 0; i < 3; ++i) tick();
    check("count holds while disabled", static_cast<int>(topp->count_o), 4);
    check("once stays one-shot", static_cast<int>(topp->once_o), 7);

    topp->en = 1;
    for (int i = 1; i <= 2; ++i) tick();
    check("count resumes", static_cast<int>(topp->count_o), 6);
    check("once never re-runs", static_cast<int>(topp->once_o), 7);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
