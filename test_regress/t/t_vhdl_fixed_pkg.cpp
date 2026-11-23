// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL ieee.fixed_pkg sfixed and ufixed
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>

static std::string bits(uint64_t value, int width) {
    std::string text;
    for (int i = width - 1; i >= 0; --i) text += ((value >> i) & 1) ? '1' : '0';
    return text;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    // One line per operand pair, FNV-1a hashed as the GHDL run's output is. Each line is
    // "a b" followed by the outputs in declaration order, as binary strings; the two
    // integer outputs are decimal.
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (int a = 0; a < 64; ++a) {
        for (int b = 0; b < 16; ++b) {
            topp->sa = a;
            topp->ua = a;
            topp->sb = b;
            topp->ub = b;
            topp->eval();
            std::string text = std::to_string(a) + " " + std::to_string(b);
            text += " " + bits(topp->s_add, 7);
            text += " " + bits(topp->s_sub, 7);
            text += " " + bits(topp->s_mul, 10);
            text += " " + bits(topp->s_neg, 7);
            text += " " + bits(topp->s_abs, 7);
            text += " " + bits(topp->u_add, 7);
            text += " " + bits(topp->u_sub, 7);
            text += " " + bits(topp->u_mul, 10);
            text += " " + bits(topp->s_lt, 1);
            text += " " + bits(topp->s_le, 1);
            text += " " + bits(topp->s_eq, 1);
            text += " " + bits(topp->s_gt, 1);
            text += " " + bits(topp->u_lt, 1);
            text += " " + bits(topp->u_le, 1);
            text += " " + bits(topp->u_eq, 1);
            text += " " + bits(topp->u_gt, 1);
            text += " " + bits(topp->s_r1, 4);
            text += " " + bits(topp->s_r2, 4);
            text += " " + bits(topp->s_r3, 4);
            text += " " + bits(topp->s_r4, 4);
            text += " " + bits(topp->s_r5, 10);
            text += " " + bits(topp->s_r6, 2);
            text += " " + bits(topp->s_r7, 3);
            text += " " + bits(topp->u_r1, 4);
            text += " " + bits(topp->u_r2, 4);
            text += " " + bits(topp->u_r3, 4);
            text += " " + bits(topp->u_r4, 4);
            text += " " + bits(topp->u_r5, 10);
            text += " " + bits(topp->u_r6, 2);
            text += " " + bits(topp->u_r7, 3);
            text += " " + bits(topp->s_slv, 6);
            text += " " + bits(topp->s_conv, 6);
            text += " " + std::to_string(static_cast<int>(topp->s_int));
            text += " " + std::to_string(static_cast<int>(topp->u_int));
            text += " " + bits(topp->s_sgn, 4);
            text += " " + bits(topp->u_uns, 4);
            text += " " + bits(topp->s_mixi, 6);
            text += " " + bits(topp->s_mixr, 6);
            text += " " + bits(topp->u_toi, 7);
            text += " " + bits(topp->s_toi, 7);
            text += "\n";
            for (const char c : text) {
                hash ^= static_cast<unsigned char>(c);
                hash *= 0x100000001b3ULL;
            }
        }
    }
    topp->final();
    delete topp;

    if (hash != 0xf0d0deeedc703e37ULL) {
        std::printf("%%Error: output hash %016" PRIx64 " does not match GHDL's\n", hash);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
