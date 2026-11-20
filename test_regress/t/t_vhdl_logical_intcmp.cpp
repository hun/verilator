// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL integer/bit comparisons mixed in a logical operator
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (int req = 0; req <= 1; ++req) {
        for (int cnt = 0; cnt <= 5; ++cnt) {
            topp->req = req;
            topp->cnt = cnt;
            topp->eval();

            const int q = static_cast<int>(topp->q);
            const int q2 = static_cast<int>(topp->q2);
            const int qgt = static_cast<int>(topp->qgt);
            const int qExp = (req == 1 && cnt == 0) ? 1 : 0;
            const int q2Exp = (cnt != 3 || req == 0) ? 1 : 0;
            const int qgtExp = (req == 1 && cnt > 1) ? 1 : 0;
            if (q != qExp) {
                std::printf("%%Error: req=%d cnt=%d q=%d exp=%d\n", req, cnt, q, qExp);
                ++fails;
            }
            if (q2 != q2Exp) {
                std::printf("%%Error: req=%d cnt=%d q2=%d exp=%d\n", req, cnt, q2, q2Exp);
                ++fails;
            }
            if (qgt != qgtExp) {
                std::printf("%%Error: req=%d cnt=%d qgt=%d exp=%d\n", req, cnt, qgt, qgtExp);
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
