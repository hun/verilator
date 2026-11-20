// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: top-level inout readback without enable pins
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

    for (int oe = 0; oe < 2; ++oe) {
        for (int d = 0; d < 2; ++d) {
            for (int ext = 0; ext < 2; ++ext) {
                topp->oe = oe;
                topp->d = d;
                topp->io = ext;
                topp->eval();
                // Without separate enable pins the two-state model ORs the external
                // value with the enabled internal driver, and internal reads see it
                const int want = ext | (oe ? d : 0);
                check("io", static_cast<int>(topp->io), want);
                check("y", static_cast<int>(topp->y), want);
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
