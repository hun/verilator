// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a bit-string literal beside an unsigned keeps its signing
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

namespace {
int check(VM_PREFIX* topp, unsigned a, unsigned wantSum) {
    topp->a = a;
    topp->eval();
    const unsigned sum = static_cast<unsigned>(topp->o);
    const unsigned carry = topp->cy != 0 ? 1u : 0u;
    if (sum != (wantSum & 0x1ffu) || carry != (wantSum >> 8)) {
        std::printf("%%Error: a = %02x: sum = %03x/%u, expected %03x\n", a, sum, carry,
                    wantSum);
        return 1;
    }
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;
    int fails = 0;
    fails += check(topp, 0x00, 0x001);
    fails += check(topp, 0x7f, 0x080);
    fails += check(topp, 0xfe, 0x0ff);
    fails += check(topp, 0xff, 0x100);
    topp->final();
    delete topp;
    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
