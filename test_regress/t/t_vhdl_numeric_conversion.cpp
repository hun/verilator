// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: numeric_std operations on converted std_logic_vector operands
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cinttypes>
#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    const auto rotl = [](unsigned v) { return ((v << 1) | (v >> 3)) & 0xFU; };
    const auto rotr = [](unsigned v) { return ((v >> 1) | (v << 3)) & 0xFU; };
    for (unsigned a = 0; a < 16; ++a) {
        for (unsigned b = 0; b < 16; ++b) {
            topp->a = a;
            topp->b = b;
            topp->eval();
            const int sa = (a & 8) ? static_cast<int>(a) - 16 : static_cast<int>(a);
            const int sb = (b & 8) ? static_cast<int>(b) - 16 : static_cast<int>(b);
            const auto check = [&](const char* name, uint64_t got, uint64_t exp) {
                if (got != exp) {
                    std::printf("%%Error: %s a=%u b=%u (signed %d %d): got %" PRIx64
                                " exp %" PRIx64 "\n",
                                name, a, b, sa, sb, got, exp);
                    ++fails;
                }
            };
            const auto word = [](int v, unsigned mask) { return static_cast<unsigned>(v) & mask; };
            // Unsigned operands
            check("u_add", topp->u_add, (a + b) & 0xF);
            check("u_sub", topp->u_sub, word(static_cast<int>(a) - static_cast<int>(b), 0xF));
            check("u_mul", topp->u_mul, a * b);
            if (b != 0) {
                check("u_div", topp->u_div, a / b);
                check("u_rem", topp->u_rem, a % b);
                check("u_mod", topp->u_mod, a % b);
            }
            check("u_cmp", topp->u_cmp,
                  (a < b) | ((a <= b) << 1) | ((a > b) << 2) | ((a >= b) << 3) | ((a == b) << 4)
                      | ((a != b) << 5));
            check("u_shift", topp->u_shift, ((a << 1) & 0xF) | (b >> 2));
            check("u_rotate", topp->u_rotate, rotl(a) ^ rotr(b));
            check("u_ext", topp->u_ext, a);
            check("u_int", topp->u_int, a);
            check("u_lit", topp->u_lit, (a + 3 + 2 * b) & 0xF);
            // Signed operands
            check("s_add", topp->s_add, word(sa + sb, 0xF));
            check("s_sub", topp->s_sub, word(sa - sb, 0xF));
            check("s_mul", topp->s_mul, word(sa * sb, 0xFF));
            if (sb != 0) {
                int mod = sa % sb;
                if (mod != 0 && ((mod < 0) != (sb < 0))) mod += sb;
                check("s_div", topp->s_div, word(sa / sb, 0xF));
                check("s_rem", topp->s_rem, word(sa % sb, 0xF));
                check("s_mod", topp->s_mod, word(mod, 0xF));
                check("i_mod", topp->i_mod, word(mod, 0xFFFFFFFF));
                check("i_rem", topp->i_rem, word(sa % sb, 0xFFFFFFFF));
            }
            check("s_cmp", topp->s_cmp,
                  (sa < sb) | ((sa <= sb) << 1) | ((sa > sb) << 2) | ((sa >= sb) << 3)
                      | ((sa == sb) << 4) | ((sa != sb) << 5));
            check("s_shift", topp->s_shift, word((sa << 1) | (sb >> 2), 0xF));
            check("s_rotate", topp->s_rotate, rotl(a) ^ rotr(b));
            check("s_ext", topp->s_ext, word(sa, 0x3F));
            check("s_int", topp->s_int, word(sa, 0xFFFFFFFF));
            check("s_neg", topp->s_neg, word(-sa + (sb < 0 ? -sb : sb), 0xF));
            check("s_lit", topp->s_lit, word(sa + 3 - 2 * sb, 0xF));
        }
    }

    // The integer division sits behind a test of its divisor, so a zero one never reaches it
    if (Verilated::gotFinish()) {
        std::printf("%%Error: the model ended the run\n");
        ++fails;
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
