// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL bit-vector generic test
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

    // u0: "10100101", u1: "11110000", u2: default (others => '1')
    static const char seed0[] = {'1', '0', '1', '0', '0', '1', '0', '1'};
    static const char seed1[] = {'1', '1', '1', '1', '0', '0', '0', '0'};
    for (int idx = 0; idx <= 7; ++idx) {
        topp->idx = idx;
        topp->eval();
        check(idx, "o0", static_cast<int>(topp->o0), seed0[idx] == '1');
        check(idx, "o1", static_cast<int>(topp->o1), seed1[7 - idx] == '1');
        check(idx, "o2", static_cast<int>(topp->o2), 1);
        check(idx, "w0", static_cast<int>(topp->w0), 0xA5);
        check(idx, "w1", static_cast<int>(topp->w1), 0xF0);
        check(idx, "w2", static_cast<int>(topp->w2), 0xFF);
        check(idx, "lo0", static_cast<int>(topp->lo0), 0);
        check(idx, "hi0", static_cast<int>(topp->hi0), 7);
        check(idx, "len0", static_cast<int>(topp->len0), 8);
        check(idx, "lo2", static_cast<int>(topp->lo2), 0);
        check(idx, "hi2", static_cast<int>(topp->hi2), 7);
        check(idx, "len2", static_cast<int>(topp->len2), 8);
        check(idx, "w3", static_cast<int>(topp->w3), 0xF);
        check(idx, "w4", static_cast<int>(topp->w4), 0xA);
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
