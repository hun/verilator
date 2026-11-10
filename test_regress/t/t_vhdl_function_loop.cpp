// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL function loops, exit/next and local updates test
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

namespace {

int popcount8(int value) {
    int count = 0;
    for (int i = 0; i < 8; ++i) count += (value >> i) & 1;
    return count;
}

int reverse8(int value) {
    int result = 0;
    for (int i = 0; i < 8; ++i) result |= ((value >> i) & 1) << (7 - i);
    return result;
}

int lowestSet(int value) {
    for (int i = 0; i < 8; ++i) {
        if ((value >> i) & 1) return i;
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (int a = 0; a < 256; ++a) {
        topp->a = a;
        topp->eval();

        const int low = lowestSet(a);
        struct Check final {
            const char* name;
            int got;
            int want;
        } checks[] = {
            {"rev", static_cast<int>(topp->rev), reverse8(a)},
            {"cnt", static_cast<int>(topp->cnt), popcount8(a)},
            {"first", static_cast<int>(topp->first), low},
            {"outer", static_cast<int>(topp->outer), low < 0 ? 16 : 2 * low + 1},
            {"c", static_cast<int>(topp->c), 205},
            {"k", static_cast<int>(topp->k), 5},
        };
        for (const Check& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: a=%d %s=%d expected %d\n", a, check.name, check.got,
                            check.want);
                ++fails;
            }
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
