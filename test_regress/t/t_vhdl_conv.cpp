// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL numeric_std conversion test
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (int a = 0; a < 16; ++a) {
        for (int b = 0; b < 256; ++b) {
            topp->a = a;
            topp->b = b;
            topp->u4 = a;
            topp->eval();

            const int y = static_cast<int>(topp->y);
            const int z = static_cast<int>(topp->z);
            const int v = static_cast<int>(topp->v);
            const int q = static_cast<int>(topp->q);
            const int r = static_cast<int>(topp->r);
            const int t2 = static_cast<int>(topp->t2);
            const int o0 = static_cast<int>(topp->o0);
            const int o1 = static_cast<int>(topp->o1);
            const int ob = static_cast<int>(topp->ob);
            const int sq = static_cast<int>(topp->sq);
            const int w = static_cast<int>(topp->w);
            const int uo = static_cast<int>(topp->uo);
            const int eqf = static_cast<int>(topp->eqf);
            const int eqt = static_cast<int>(topp->eqt);
            const int lh = static_cast<int>(topp->lh);
            const int xz = static_cast<int>(topp->xz);
            const int ch = static_cast<int>(topp->ch);
            const int at0 = static_cast<int>(topp->at0);
            const int at1 = static_cast<int>(topp->at1);
            const int vt0 = static_cast<int>(topp->vt0);
            const int vt1 = static_cast<int>(topp->vt1);
            const int red_and = static_cast<int>(topp->red_and);
            const int red_or = static_cast<int>(topp->red_or);
            const int red_xor = static_cast<int>(topp->red_xor);
            const int red_nand = static_cast<int>(topp->red_nand);
            const int red_call = static_cast<int>(topp->red_call);
            const int red_func = static_cast<int>(topp->red_func);
            const int imp = static_cast<int>(topp->imp);
            const int conv1 = static_cast<int>(topp->conv1);
            const int conv2 = static_cast<int>(topp->conv2);
            if (topp->wide != 0x123456789aULL) {
                std::printf("%%Error: wide=%llx\n", static_cast<unsigned long long>(topp->wide));
                ++fails;
            }
            if (uo != ((~a) & 0xf)) {
                std::printf("%%Error: a=%d uo=%d\n", a, uo);
                ++fails;
            }
            if (eqf != 0 || eqt != 1) {
                std::printf("%%Error: eqf=%d eqt=%d\n", eqf, eqt);
                ++fails;
            }
            if (lh != 0x6 || ch != 1) {
                std::printf("%%Error: lh=%d ch=%d\n", lh, ch);
                ++fails;
            }
            // The unknown bits stay unknown; the known bits retain their positions
            if ((xz & 0xC) != 0x4) {
                std::printf("%%Error: xz=%d\n", xz);
                ++fails;
            }
            if (at0 != ((b >> 4) & 0xf) || at1 != (b & 0xf)) {
                std::printf("%%Error: b=%d at0=%d at1=%d\n", b, at0, at1);
                ++fails;
            }
            if (vt0 != a || vt1 != a) {
                std::printf("%%Error: a=%d vt0=%d vt1=%d\n", a, vt0, vt1);
                ++fails;
            }
            const int wantAnd = (a == 0xf) ? 1 : 0;
            const int wantOr = (a != 0) ? 1 : 0;
            const int wantXor = (__builtin_popcount(a) & 1) ? 1 : 0;
            if (imp != (a & 1)) {
                std::printf("%%Error: a=%d imp=%d\n", a, imp);
                ++fails;
            }
            if (conv1 != a || conv2 != a) {
                std::printf("%%Error: a=%d conv1=%d conv2=%d\n", a, conv1, conv2);
                ++fails;
            }
            if (red_and != wantAnd || red_func != wantAnd || red_or != wantOr
                || red_call != wantOr || red_xor != wantXor || red_nand != !wantAnd) {
                std::printf("%%Error: a=%d reductions %d %d %d %d %d %d\n", a, red_and,
                            red_or, red_xor, red_nand, red_call, red_func);
                ++fails;
            }
            if (y != a) {
                std::printf("%%Error: a=%d y=%d\n", a, y);
                ++fails;
            }
            if (z != 0xf) {
                std::printf("%%Error: a=%d z=%d\n", a, z);
                ++fails;
            }
            if (v != 0xff) {
                std::printf("%%Error: a=%d v=%d\n", a, v);
                ++fails;
            }
            if (q != ((b >> 4) & 0xf)) {
                std::printf("%%Error: b=%d q=%d\n", b, q);
                ++fails;
            }
            if (r != 0x0f) {
                std::printf("%%Error: r=%d\n", r);
                ++fails;
            }
            if (t2 != 0xaa) {
                std::printf("%%Error: t2=%d\n", t2);
                ++fails;
            }
            const int agg0 = static_cast<int>(topp->agg0);
            const int agg1 = static_cast<int>(topp->agg1);
            const int agg2 = static_cast<int>(topp->agg2);
            const int agg3 = static_cast<int>(topp->agg3);
            const int wantAgg0
                = (((a >> 2) & 1) ? 0x01 : 0x00) | (((a >> 3) & 1) ? 0xfe : 0x00);
            if (agg0 != wantAgg0) {
                std::printf("%%Error: a=%d agg0=%d\n", a, agg0);
                ++fails;
            }
            if (agg1 != (((a & 1) ? 0xf0 : 0x00) | ((a & 2) ? 0x0f : 0x00))) {
                std::printf("%%Error: a=%d agg1=%d\n", a, agg1);
                ++fails;
            }
            if (agg2 != a) {
                std::printf("%%Error: a=%d agg2=%d\n", a, agg2);
                ++fails;
            }
            if (agg3 != ((((a >> 1) & 1) ? 0x8 : 0) | (((a >> 2) & 1) ? 0x1 : 0))) {
                std::printf("%%Error: a=%d agg3=%d\n", a, agg3);
                ++fails;
            }
            if (o0 != 0x00 || o1 != 0xff || ob != ((b & 1) ? 0xff : 0x00)) {
                std::printf("%%Error: b=%d o0=%d o1=%d ob=%d\n", b, o0, o1, ob);
                ++fails;
            }
            if (sq != 0xf8) {
                std::printf("%%Error: sq=%d\n", sq);
                ++fails;
            }
            if (w != a) {
                std::printf("%%Error: a=%d w=%d\n", a, w);
                ++fails;
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
