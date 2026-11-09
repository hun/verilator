// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL non-static slice test
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };

    const int avals[] = {0x1234, 0xabcd, 0x00ff, 0xff00};
    topp->clk = 0;
    topp->we = 0;
    topp->waddr = 0;
    topp->din = 0;
    topp->eval();
    for (const int a : avals) {
        topp->a = a;
        for (int i = 0; i < 4; ++i) {
            topp->i = i;
            topp->eval();
            check("y", static_cast<int>(topp->y), (a >> ((i & 1) * 8)) & 0xff);
            check("z", static_cast<int>(topp->z), (a >> (i * 2)) & 3);
            check("ay", static_cast<int>(topp->ay), (a >> ((i & 1) ? 0 : 8)) & 0xff);
        }
    }

    // Dynamic slice as an assignment target
    const auto write = [&](int addr, int data) {
        topp->waddr = addr;
        topp->din = data;
        topp->we = 1;
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
    };
    write(2, 0x5a);
    write(0, 0xa5);
    topp->we = 0;
    for (int i = 0; i < 4; ++i) {
        topp->i = i;
        topp->eval();
        const int want = i == 2 ? 0x5a : (i == 0 ? 0xa5 : 0);
        check("q", static_cast<int>(topp->q), want);
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
