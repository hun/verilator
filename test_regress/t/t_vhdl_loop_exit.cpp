// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL exit and next in static loops
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
    const auto check = [&](const char* name, int got, int want, int v) {
        if (got != want) {
            std::printf("%%Error: v=%02x %s=%d expected %d\n", v, name, got, want);
            ++fails;
        }
    };

    for (int v = 0; v < 256; ++v) {
        topp->v = v;
        topp->eval();

        int wantIdx = 0;
        int wantFound = 0;
        for (int i = 0; i < 8; ++i) {
            if (v & (1 << i)) {
                wantIdx = i;
                wantFound = 1;
                break;
            }
        }
        int wantCnt = 0;
        for (int i = 0; i < 8; ++i) wantCnt += (v >> i) & 1;
        int wantPairs = 0;
        for (int i = 0; i < 4; ++i) {
            if (v & (3 << (i * 2))) ++wantPairs;
        }

        check("idx", static_cast<int>(topp->idx), wantIdx, v);
        check("found", static_cast<int>(topp->found), wantFound, v);
        check("cnt", static_cast<int>(topp->cnt), wantCnt, v);
        check("pairs", static_cast<int>(topp->pairs), wantPairs, v);
        check("latch", static_cast<int>(topp->latch), 1, v);
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
