// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL subtype mark used as a slice range test
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
    // The slice named by each subtype of the 8-bit input, and the elements written
    // through a subtype-named slice of an internal signal
    const struct {
        int addr;
        int low;
        int high;
    } vectors[] = {{0xB2, 2, 11}, {0x5E, 14, 5}};
    for (const auto& vector : vectors) {
        topp->i_addr = vector.addr;
        topp->eval();
        const struct {
            const char* name;
            int got;
            int want;
        } checks[] = {{"o_low", static_cast<int>(topp->o_low), vector.low},
                      {"o_high", static_cast<int>(topp->o_high), vector.high},
                      {"o_write", static_cast<int>(topp->o_write), 5}};
        for (const auto& check : checks) {
            if (check.got != check.want) {
                std::printf("%%Error: i_addr=%02x: %s=%d expected %d\n", vector.addr, check.name,
                            check.got, check.want);
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
