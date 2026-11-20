// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a user operator over another type leaves the built-in one
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

namespace {
int check(VM_PREFIX* topp, unsigned a, unsigned b) {
    topp->a = a;
    topp->b = b;
    topp->eval();
    const unsigned z = (a * 4u + b) & 0x3fu;
    const unsigned wantXor = (z & 0xfu) ^ b;
    const unsigned wantPol = (~b) & 0xfu;
    int fails = 0;
    if (topp->o_left != wantXor) {
        std::printf("%%Error: a=%x b=%x o_left=%x, expected %x\n", a, b,
                    static_cast<unsigned>(topp->o_left), wantXor);
        ++fails;
    }
    if (topp->o_right != wantXor) {
        std::printf("%%Error: a=%x b=%x o_right=%x, expected %x\n", a, b,
                    static_cast<unsigned>(topp->o_right), wantXor);
        ++fails;
    }
    if (topp->o_pol != wantPol) {
        std::printf("%%Error: a=%x b=%x o_pol=%x, expected %x\n", a, b,
                    static_cast<unsigned>(topp->o_pol), wantPol);
        ++fails;
    }
    return fails;
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;
    int fails = 0;
    fails += check(topp, 0x1, 0x3);
    fails += check(topp, 0x0, 0xf);
    fails += check(topp, 0x7, 0x9);
    fails += check(topp, 0xf, 0x0);
    fails += check(topp, 0x2, 0x6);
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
