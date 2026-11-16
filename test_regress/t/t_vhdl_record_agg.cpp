// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL record aggregate with an others choice
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
    const auto check = [&](const char* what, CData got, CData want) {
        if (got != want) {
            std::printf("%%Error: %s=%02x exp=%02x\n", what, got, want);
            ++fails;
        }
    };
    // (ack => '0', stall => '0', others => (others => '0'))
    check("o", topp->o, 0x00);
    check("a", topp->a, 0);
    // (dat => x"3C", ack => '1', others => '0')
    check("p", topp->p, 0x3c);
    check("q", topp->q, 0);
    // (dat => "10101010", others => '1')
    check("r", topp->r, 0xaa);
    check("s", topp->s, 1);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
