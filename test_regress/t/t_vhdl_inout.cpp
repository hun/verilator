// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL top-level inout ports test
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
        for (int d = 0; d < 16; ++d) {
            for (int ext = 0; ext < 16; ++ext) {
                topp->oe = oe;
                topp->d = d & 1;
                topp->dv = d;
                // The pin carries the value resolved outside the model
                topp->io = ext & 1;
                topp->ios = ext & 1;
                topp->iov = ext;
                topp->eval();
                check("y", static_cast<int>(topp->y), ext & 1);
                check("ys", static_cast<int>(topp->ys), ext & 1);
                check("yv", static_cast<int>(topp->yv), ext);
                check("io__en", static_cast<int>(topp->io__en), oe);
                check("io__out", static_cast<int>(topp->io__out), oe ? (d & 1) : 0);
                check("ios__en", static_cast<int>(topp->ios__en), oe);
                check("ios__out", static_cast<int>(topp->ios__out), oe ? (d & 1) : 0);
                check("iov__en", static_cast<int>(topp->iov__en), oe ? 0xf : 0);
                check("iov__out", static_cast<int>(topp->iov__out), oe ? d : 0);
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
