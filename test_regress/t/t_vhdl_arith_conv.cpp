// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL std_logic_arith conversion builtin test
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

    int fails = 0;
    static const int inV[4] = {0x01, 0x81, 0x5a, 0xff};
    for (const int v : inV) {
        for (int n = 0; n <= 9; ++n) {
            topp->v = v;
            topp->n = n;
            topp->eval();
            const int shl = n >= 8 ? 0 : (v << n) & 0xff;
            const int shr = n >= 8 ? 0 : (v >> n) & 0xff;
            if ((static_cast<int>(topp->sl) & 0xff) != shl
                || (static_cast<int>(topp->sr) & 0xff) != shr) {
                std::printf("%%Error: v=%02x n=%d sl=%02x sr=%02x exp %02x %02x\n", v, n,
                            static_cast<int>(topp->sl) & 0xff,
                            static_cast<int>(topp->sr) & 0xff, shl, shr);
                ++fails;
            }
        }
    }
    topp->v = 0;
    topp->n = 0;
    topp->eval();
    const struct {
        const char* name;
        int got;
        int want;
    } checks[] = {
        {"u", static_cast<int>(topp->u) & 0xff, 5},
        {"s", static_cast<int>(topp->s) & 0xff, 0xff},
        {"w", static_cast<int>(topp->w) & 0xff, 5},
        {"x", static_cast<int>(topp->x) & 0xff, 0xff},
    };
    for (const auto& check : checks) {
        if (check.got != check.want) {
            std::printf("%%Error: %s=%d expected %d\n", check.name, check.got, check.want);
            ++fails;
        }
    }

    // std_logic_arith comparisons widen an unsigned operand to one bit more than its
    // width before comparing as signed, and compare a signed operand with an integer at
    // the signed operand's width, so the integer truncates to that width.
    const auto truncSigned = [](int value, int width) {
        const int modulus = 1 << width;
        int result = value % modulus;
        if (result < 0) result += modulus;
        if (result >= (modulus >> 1)) result -= modulus;
        return result;
    };
    for (int ub = 0; ub < 16; ++ub) {
        for (int sb = 0; sb < 16; ++sb) {
            for (int wb = 0; wb < 8; ++wb) {
                for (int xb = 0; xb < 8; ++xb) {
                    for (int iv = -16; iv <= 15; ++iv) {
                        topp->u4 = ub;
                        topp->s4 = sb;
                        topp->u3 = wb;
                        topp->s3 = xb;
                        topp->i = iv;
                        topp->eval();
                        const int s4v = truncSigned(sb, 4);
                        const int s3v = truncSigned(xb, 3);
                        const int i4 = truncSigned(iv, 4);
                        const struct {
                            const char* name;
                            int got;
                            int want;
                        } cmps[] = {
                            {"lt_uu", static_cast<int>(topp->lt_uu), ub < wb},
                            {"lt_ss", static_cast<int>(topp->lt_ss), s4v < s3v},
                            {"lt_us", static_cast<int>(topp->lt_us), ub < s4v},
                            {"eq_us", static_cast<int>(topp->eq_us), ub == s4v},
                            {"lt_su", static_cast<int>(topp->lt_su), s4v < ub},
                            {"lt_ui", static_cast<int>(topp->lt_ui), ub < iv},
                            {"lt_iu", static_cast<int>(topp->lt_iu), iv < ub},
                            {"eq_ui", static_cast<int>(topp->eq_ui), ub == iv},
                            {"lt_si", static_cast<int>(topp->lt_si), s4v < i4},
                            {"lt_is", static_cast<int>(topp->lt_is), i4 < s4v},
                            {"eq_si", static_cast<int>(topp->eq_si), s4v == i4},
                        };
                        for (const auto& cmp : cmps) {
                            if (cmp.got != cmp.want) {
                                std::printf("%%Error: %s u4=%d s4=%d u3=%d s3=%d i=%d got %d"
                                            " expected %d\n",
                                            cmp.name, ub, s4v, wb, s3v, iv, cmp.got, cmp.want);
                                ++fails;
                            }
                        }
                    }
                }
            }
        }
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
