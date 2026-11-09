// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL string generic test
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
    const auto check = [&](int idx, const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: idx=%d %s=%d expected %d\n", idx, name, got, want);
            ++fails;
        }
    };

    // seq "0_1-" has index range 1 to 4; "10_-0_" has range 1 to 6
    static const char seq0[] = {'x', '0', '_', '1', '-'};
    static const char seq1[] = {'x', '1', '0', '_', '-', '0', '_'};
    for (int idx = 1; idx <= 4; ++idx) {
        topp->idx = idx;
        topp->eval();
        check(idx, "lo0", static_cast<int>(topp->lo0), 1);
        check(idx, "hi0", static_cast<int>(topp->hi0), 4);
        check(idx, "len0", static_cast<int>(topp->len0), 4);
        const bool bit0 = seq0[idx] == '1' || seq0[idx] == '-';
        check(idx, "bit0", static_cast<int>(topp->bit0), bit0);
        const bool bit1 = seq1[idx] == '1' || seq1[idx] == '-';
        check(idx, "bit1", static_cast<int>(topp->bit1), bit1);
    }
    for (int idx = 5; idx <= 6; ++idx) {
        topp->idx = idx;
        topp->eval();
        const bool bit1 = seq1[idx] == '1' || seq1[idx] == '-';
        check(idx, "bit1", static_cast<int>(topp->bit1), bit1);
    }
    topp->idx = 6;
    topp->eval();
    check(6, "lo1", static_cast<int>(topp->lo1), 1);
    check(6, "hi1", static_cast<int>(topp->hi1), 6);
    check(6, "len1", static_cast<int>(topp->len1), 6);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
