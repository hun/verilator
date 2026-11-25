// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL enumeration 'image in a report test
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    // The report prints the state registered by the previous edge
    for (int value = 0; value < 3; ++value) {
        topp->s = value;
        for (int edge = 0; edge < 2; ++edge) {
            topp->clk = 0;
            topp->eval();
            topp->clk = 1;
            topp->eval();
        }
    }
    delete topp;
    return 0;
}
