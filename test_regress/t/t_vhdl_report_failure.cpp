// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL report severity failure stops the run
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
    // A failure stops the model without aborting, so the checks below still run
    Verilated::fatalOnError(false);
    VM_PREFIX* const topp = new VM_PREFIX;

    topp->eval();
    int fails = 0;
    if (!Verilated::gotError()) {
        std::printf("%%Error: the run did not fail\n");
        ++fails;
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
