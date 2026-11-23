// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a positional aggregate whose elements are slices
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

    topp->i = 0x12345678;
    topp->eval();

    int fails = 0;
    const auto check = [&](const char* name, uint32_t got, uint32_t want) {
        if (got != want) {
            std::printf("%%Error: %s=%x expected %x\n", name, got, want);
            ++fails;
        }
    };
    // The bytes rotate left by one position
    check("o", topp->o, 0x34567812);
    // i(3 downto 0) = 8 in the high nibble, then bits 7..4 = 0111 in the low nibble
    check("m", topp->m, 0x87);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
