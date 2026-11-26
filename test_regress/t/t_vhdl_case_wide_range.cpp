// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a case choice range too wide to expand
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdint>
#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    struct Item {
        int32_t in;
        int out;
    };
    const Item cases[] = {{100, 1},
                          {2147483647, 1},
                          {99, 0},
                          {50, 0},
                          {10, 6},
                          {9, 5},
                          {0, 5},
                          {-1, 0},
                          {-99, 0},
                          {-100, -1},
                          {-2147483647 - 1, -1}};
    for (const Item& item : cases) {
        topp->x = static_cast<uint32_t>(item.in);
        topp->eval();
        if (static_cast<int8_t>(topp->y) != item.out) {
            std::printf("%%Error: x=%d y=%d exp=%d\n", item.in,
                        static_cast<int>(static_cast<int8_t>(topp->y)), item.out);
            ++fails;
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
