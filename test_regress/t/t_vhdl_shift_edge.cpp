// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL bit_vector arithmetic shift fill
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

    for (int v = 0; v < 16; ++v) {
        for (int n = 0; n <= 5; ++n) {
            const int sign = (v >> 3) & 1;
            const int lsb = v & 1;
            int sra = 0;
            int sla = 0;
            if (n >= 4) {
                sra = sign ? 15 : 0;
                sla = lsb ? 15 : 0;
            } else {
                sra = ((v | (sign ? ~15 : 0)) >> n) & 15;
                sla = ((v << n) | (lsb ? ((1 << n) - 1) : 0)) & 15;
            }
            const int sll = n >= 4 ? 0 : (v << n) & 15;
            const int srl = n >= 4 ? 0 : (v >> n) & 15;

            topp->b = v;
            topp->n = n;
            topp->eval();
            check("sra", static_cast<int>(topp->sra_o), sra);
            check("sla", static_cast<int>(topp->sla_o), sla);
            check("sll", static_cast<int>(topp->sll_o), sll);
            check("srl", static_cast<int>(topp->srl_o), srl);
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
