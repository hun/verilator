// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: wide case choice ranges in functions and selected assignments
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
        int y;
        int z;
        int w;
        int sel;
        int vv;
        int pp;
        int ll;
    };
    // in, y, z, w, sel, v, p, lp
    const Item cases[]
        = {{100, 1, 10, 7, 100, 1, 11, 3},     {2147483645, 1, 10, 7, 100, 1, 11, 3},
           {99, 0, 3, 9, 300, 2, 33, 12},      {98, 0, 3, 9, 300, 2, 33, 21},
           {50, 0, 3, 9, 300, 2, 33, 30},      {9, 5, 3, 9, 300, 2, 33, 30},
           {7, 5, 3, 9, 300, 77, 33, 30},      {0, 5, 3, 9, 300, 2, 33, 30},
           {-1, 0, 3, 8, 300, 2, 33, 30},      {-99, 0, 3, 8, 300, 2, 33, 30},
           {-100, -1, -10, 8, 200, 2, 22, 30}, {-2147483647 - 1, -1, -10, 8, 200, 2, 22, 30}};
    for (const Item& item : cases) {
        topp->x = static_cast<uint32_t>(item.in);
        topp->eval();
        if (topp->y != item.y || topp->z != item.z || topp->w != item.w || topp->sel != item.sel
            || topp->v != item.vv || topp->p != item.pp || topp->lp != item.ll) {
            std::printf("%%Error: x=%d y=%d z=%d w=%d sel=%d exp=%d %d %d %d\n", item.in,
                        static_cast<int>(topp->y), static_cast<int>(topp->z),
                        static_cast<int>(topp->w), static_cast<int>(topp->sel), item.y, item.z,
                        item.w, item.sel);
            ++fails;
        }
    }
    // 1 + -1 + 5 + 0, then 3 + 30
    if (topp->k != 38) {
        std::printf("%%Error: k=%d exp=38\n", static_cast<int>(topp->k));
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
