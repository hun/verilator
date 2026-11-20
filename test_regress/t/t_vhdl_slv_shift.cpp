// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL predefined and numeric_std shift fill
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

    for (int value = 0; value < 16; ++value) {
        for (int n = 0; n <= 5; ++n) {
            const int sign = (value >> 3) & 1;
            const int lsb = value & 1;
            int svSra = 0;
            int svSla = 0;
            if (n >= 4) {
                svSra = sign ? 15 : 0;
                svSla = lsb ? 15 : 0;
            } else {
                svSra = ((value | (sign ? ~15 : 0)) >> n) & 15;
                svSla = ((value << n) | (lsb ? ((1 << n) - 1) : 0)) & 15;
            }
            const int uSra = n >= 4 ? 0 : (value >> n) & 15;
            const int uSla = n >= 4 ? 0 : (value << n) & 15;

            topp->sv = value;
            topp->u = value;
            topp->n = n;
            topp->eval();
            check("sv_sra", static_cast<int>(topp->sv_sra), svSra);
            check("sv_sla", static_cast<int>(topp->sv_sla), svSla);
            check("u_sra", static_cast<int>(topp->u_sra), uSra);
            check("u_sla", static_cast<int>(topp->u_sla), uSla);
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
