// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a std.textio file builds a memory image at elaboration
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
    // The image is "AB" then "FF01": hread takes two digits for eight bits
    const auto check = [&](const char* what, CData got, CData want) {
        if (got != want) {
            std::printf("%%Error: %s=%02x exp=%02x\n", what, got, want);
            ++fails;
        }
    };
    check("o0", topp->o0, 0xab);
    check("o1", topp->o1, 0xff);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
