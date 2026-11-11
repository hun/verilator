// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL array range test
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

    topp->d = 0xA;
    topp->a = 0xA;
    topp->eval();

    int fails = 0;
    const auto check = [&](const char* name, int actual, int expected) {
        if (actual == expected) return;
        std::printf("%%Error: %s=%d expected %d\n", name, actual, expected);
        ++fails;
    };
    check("d(0)", static_cast<int>(topp->o_d0), 0);
    check("d(3)", static_cast<int>(topp->o_d3), 1);
    check("a(0)", static_cast<int>(topp->o_a0), 1);
    check("a(3)", static_cast<int>(topp->o_a3), 0);
    check("a(1 to 2)", static_cast<int>(topp->o_sl), 0x1);
    check("ascending aggregate", static_cast<int>(topp->o_ca), 0x8);
    check("reverse range", static_cast<int>(topp->o_rr), 1);
    check("generic range", static_cast<int>(topp->o_g), 0xFF);
    check("attribute range", static_cast<int>(topp->o_rng), 0xF);
    check("ascending range association", static_cast<int>(topp->o_av), 0x6);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
