// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL 'Z' logic literal lowering test
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };
    // A high-impedance 'Z' has no driver, so in the two-state model it settles low:
    // scalar 'Z, the 'z bit of "0z10" (-> 0b0010), and (others => 'Z) (-> 0) all read 0.
    const auto checkAll = [&](int qz, int qv, int qo) {
        check("qz", static_cast<int>(topp->qz), qz);
        check("qv", static_cast<int>(topp->qv), qv);
        check("qo", static_cast<int>(topp->qo), qo);
    };

    // Load d while enabled
    topp->en = 1;
    topp->d = 1;
    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    checkAll(1, 0x2, 0);

    // Released: s takes 'Z, which reads low; the vector constants are unchanged
    topp->en = 0;
    topp->clk = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    checkAll(0, 0x2, 0);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
