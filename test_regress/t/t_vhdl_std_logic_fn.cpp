// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: the std_logic_1164 X01 conversions and if_then_else
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%x expected %x\n", name, got, want);
            ++fails;
        }
    };
    // 1010 and 0110 survive the conversions
    check("o_x01", static_cast<int>(topp->o_x01), 0xa);
    check("o_01", static_cast<int>(topp->o_01), 0x6);
    check("o_z", static_cast<int>(topp->o_z), 0x6);
    check("o_ux", static_cast<int>(topp->o_ux), 0xa);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}