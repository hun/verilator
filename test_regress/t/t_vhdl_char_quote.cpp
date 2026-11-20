// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: the quote character literal
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

    topp->eval();
    // The literal for a single quote is three quotes
    const bool ok = static_cast<char>(topp->o) == '\'';
    if (!ok) std::printf("%%Error: o=%d expected %d\n", static_cast<int>(topp->o), '\'');
    topp->final();
    delete topp;
    if (!ok) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}