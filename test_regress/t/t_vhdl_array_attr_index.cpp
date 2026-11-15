// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL index-range attributes A'LEFT(N) .. A'RANGE(N)
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

    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    topp->eval();

    int fails = 0;
    const auto check = [&fails](const char* name, int got, int expect) {
        if (got != expect) {
            std::printf("%%Error: %s=%d exp=%d\n", name, got, expect);
            ++fails;
        }
    };
    // m is ("1011", "0100"), so row 0 is 4, row 1 is 0xb and row 2 was never written.
    // copy is "1101"; m'range(2) is the inner range (4 bits), m'range(1) the outer (2).
    check("o0", topp->o0, 4);
    check("o1", topp->o1, 0xb);
    check("o2", topp->o2, 0);
    check("full", topp->full, 0xd);
    check("part", topp->part, 1);
    check("len", topp->len, 24);  // m'length(1) = 2 rows, m'length(2) = 4 bits
    check("lo", topp->lo, 0);  // m'low(2)
    check("hi", topp->hi, 1);  // m'high(1)
    check("lft", topp->lft, 3);  // m'left(2)
    check("rgt", topp->rgt, 0);  // m'right(2)
    check("asc", topp->asc, 1);  // n'ascending(1)
    check("dsc", topp->dsc, 0);  // n'ascending(2)
    check("ones", topp->ones, 4);  // '1' bits of both rows

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
