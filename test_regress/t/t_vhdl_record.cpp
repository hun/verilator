// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL record types through GHDL XML test
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
    const int dinHi = 1;
    const int dinLo = 2;

    topp->clk = 0;
    topp->din = 0;
    topp->eval();

    // Packed layout: en=[9], st=[8], num=[7:4], inr.hi=[3:2], inr.lo=[1:0]
    for (int en = 0; en < 2; ++en) {
        for (int num = 0; num < 16; num += 5) {
            const unsigned din
                = (static_cast<unsigned>(en) << 9) | (1U << 8) | (static_cast<unsigned>(num) << 4)
                  | (static_cast<unsigned>(dinHi) << 2) | static_cast<unsigned>(dinLo);
            topp->clk = 1;
            topp->din = din;
            topp->eval();

            const unsigned dout = static_cast<unsigned>(topp->dout);
            const int dEn = (dout >> 9) & 1;
            const int dSt = (dout >> 8) & 1;
            const int dNum = (dout >> 4) & 15;
            const int dHi = (dout >> 2) & 3;
            const int dLo = dout & 3;
            const int y = static_cast<int>(topp->y);
            const int zero = static_cast<int>(topp->zero_o);
            if (en) {
                if (dEn != 1 || dSt != 1 || dNum != num || dHi != dinLo || dLo != dinHi) {
                    std::printf("%%Error: en=1 num=%d dout=%x\n", num, dout);
                    ++fails;
                }
                if (static_cast<int>(topp->st_o) != 1) {
                    std::printf("%%Error: st_o=%d\n", static_cast<int>(topp->st_o));
                    ++fails;
                }
                if (y != ((num << 5) | (dinLo << 3) | (dinHi << 1) | 1)) {
                    std::printf("%%Error: en=1 num=%d y=%x\n", num, y);
                    ++fails;
                }
                if (zero != 0) {
                    std::printf("%%Error: en=1 zero_o=%d\n", zero);
                    ++fails;
                }
            } else {
                if (dout != 0) {
                    std::printf("%%Error: en=0 dout=%x\n", dout);
                    ++fails;
                }
                if (y != 0) {
                    std::printf("%%Error: en=0 y=%x\n", y);
                    ++fails;
                }
                if (zero != 1) {
                    std::printf("%%Error: en=0 zero_o=%d\n", zero);
                    ++fails;
                }
            }
            topp->clk = 0;
            topp->eval();
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
