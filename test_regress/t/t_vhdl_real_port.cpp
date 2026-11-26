// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL real ports, top level and through an instance
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

    int errors = 0;
    const auto check = [&](const char* name, double got, double want) {
        if (got != want) {
            std::printf("%%Error: %s=%g expected %g\n", name, got, want);
            ++errors;
        }
    };
    topp->p = 1.5;
    topp->eval();
    check("q", topp->q, 3.5);
    check("g", topp->g, 1);
    check("h", topp->h, 2.5);
    topp->p = 0.25;
    topp->eval();
    check("q", topp->q, 1.0);
    check("g", topp->g, 0);
    check("h", topp->h, 1.25);

    topp->final();
    delete topp;
    if (errors) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
