// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL generic subprogram parameter test
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
    // inv_inst binds id => inv (not), id_inst binds id => passthrough (identity).
    for (int in = 0; in <= 1; ++in) {
        topp->i = static_cast<CData>(in);
        topp->eval();
        topp->eval();  // Let the combinational outputs settle
        const int expNot = in ? 0 : 1;
        if (static_cast<int>(topp->o_not) != expNot) {
            std::printf("%%Error: i=%d o_not=%d expected %d\n", in, static_cast<int>(topp->o_not),
                        expNot);
            ++fails;
        }
        if (static_cast<int>(topp->o_id) != in) {
            std::printf("%%Error: i=%d o_id=%d expected %d\n", in, static_cast<int>(topp->o_id),
                        in);
            ++fails;
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
