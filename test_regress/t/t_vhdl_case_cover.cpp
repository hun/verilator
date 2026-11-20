// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a case that covers every value of its selector
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
    // The enum literals are encoded by position: IDLE 0, RUN 1, DONE 2
    static const int inputs[] = {0, 1, 2};
    for (int i = 0; i < 3; ++i) {
        topp->s = inputs[i];
        topp->eval();
        if (topp->o != inputs[i]) {
            std::printf("%%Error: s=%d o=%d exp=%d\n", inputs[i], topp->o, inputs[i]);
            ++fails;
        }
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}