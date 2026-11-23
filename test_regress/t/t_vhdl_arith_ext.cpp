// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: ext and sxt of std_logic_arith
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

    topp->v = 0x9;
    topp->eval();
    check("ez neg", topp->ez, 0x09);
    check("es neg", topp->es, 0xf9);
    check("tz neg", topp->tz, 0x1);
    check("ts neg", topp->ts, 0x1);

    topp->v = 0x6;
    topp->eval();
    check("ez pos", topp->ez, 0x06);
    check("es pos", topp->es, 0x06);
    check("tz pos", topp->tz, 0x2);
    check("ts pos", topp->ts, 0x2);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
