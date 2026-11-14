// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL multi-dimensional array test
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
    // The rows are declared most significant index first, so sel counts them down
    const int expO[4] = {4, 3, 2, 1};
    for (int sel = 0; sel < 4; ++sel) {
        topp->sel = sel;
        topp->eval();
        check("o", topp->o, expO[sel]);
        // bits(i, i): the diagonal 0, 0, 1, 0 of the rows 0011 1010 1111 0000
        check("o2", topp->o2, 2);
        check("o3", topp->o3, 1);  // quad(3, 1)
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
