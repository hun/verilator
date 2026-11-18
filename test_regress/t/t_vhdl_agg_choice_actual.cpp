// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a range-choice aggregate as an unconstrained formal actual
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

    topp->eval();
    int fails = 0;
    // The aggregates are all-ones or all-zeros, so the only value that can tell the
    // overloads apart is where the choice's range lands
    if (topp->sz != 0) {
        std::printf("%%Error: sz=%d expected 0\n", static_cast<int>(topp->sz));
        ++fails;
    }
    if (topp->so != 15) {
        std::printf("%%Error: so=%d expected 15\n", static_cast<int>(topp->so));
        ++fails;
    }
    for (unsigned a = 0; a < 16; ++a) {
        topp->a = a;
        topp->eval();
        const unsigned want = (a & 8) ? 0xf : 0x0;
        if (topp->f != want) {
            std::printf("%%Error: a=%x f=%x expected %x\n", a, static_cast<unsigned>(topp->f),
                        want);
            ++fails;
        }
    }
    // Cnt += (Cnt(3) ? 4 : 15) + add
    unsigned cnt = 0;
    for (unsigned add = 0; add < 16; ++add) {
        topp->add = add;
        for (int step = 0; step < 7; ++step) {
            cnt = (cnt + ((cnt & 8) ? 4 : 15) + add) & 0xf;
            topp->clk = 0;
            topp->eval();
            topp->clk = 1;
            topp->eval();
            if (topp->y != cnt) {
                std::printf("%%Error: add=%x step=%d y=%x expected %x\n", add, step,
                            static_cast<unsigned>(topp->y), cnt);
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
