// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL operator test
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
    const int avals[] = {-5, -1, 0, 3, 7};
    const int bvals[] = {-3, -2, 1, 2, 4};

    topp->u = 0;
    topp->s = 0;
    topp->eval();

    for (const int a : avals) {
        for (const int b : bvals) {
            for (int u = 0; u < 16; ++u) {
                const int sv = u >= 8 ? u - 16 : u;
                topp->a = a;
                topp->b = b;
                topp->u = u;
                topp->s = u & 15;
                topp->w = u + 1;
                topp->sw = u + 1;
                topp->eval();

                const int rem = a % b;
                const int mod = (rem != 0 && ((rem < 0) != (b < 0))) ? rem + b : rem;
                const int uu = u & 15;
                const int ww = u + 1;
                const int prev = (uu - 1) & 15;
                const int andValue = uu & prev;
                const int ssllBits = (sv << 1) & 15;
                const int ssrlBits = (u & 15) >> 1;
                const int ssraBits = (sv >> 1) & 15;
                struct Check final {
                    const char* name;
                    int got;
                    int want;
                } checks[] = {
                    {"mul", static_cast<int>(topp->mul_o), a * b},
                    {"div", static_cast<int>(topp->div_o), a / b},
                    {"mod", static_cast<int>(topp->mod_o), mod},
                    {"rem", static_cast<int>(topp->rem_o), rem},
                    {"pow", static_cast<int>(topp->pow_o), a * a},
                    {"abs", static_cast<int>(topp->abs_o), a < 0 ? -a : a},
                    {"neg", static_cast<int>(topp->neg_o), -a - (b - 1)},
                    {"lt", static_cast<int>(topp->lt_o), a < b},
                    {"le", static_cast<int>(topp->le_o), a <= b},
                    {"gt", static_cast<int>(topp->gt_o), a > b},
                    {"ge", static_cast<int>(topp->ge_o), a >= b},
                    {"and", static_cast<int>(topp->and_o), andValue},
                    {"nand", static_cast<int>(topp->nand_o), (~andValue) & 15},
                    {"nor", static_cast<int>(topp->nor_o), (~(uu | prev)) & 15},
                    {"xnor", static_cast<int>(topp->xnor_o), (~(uu ^ prev)) & 15},
                    {"sll", static_cast<int>(topp->sll_o), (uu << 1) & 15},
                    {"srl", static_cast<int>(topp->srl_o), uu >> 2},
                    {"sra", static_cast<int>(topp->sra_o), uu >> 1},
                    {"ssll", static_cast<int>(topp->ssll_o) & 15, ssllBits},
                    {"ssrl", static_cast<int>(topp->ssrl_o) & 15, ssrlBits},
                    {"ssla", static_cast<int>(topp->ssla_o) & 15, ssllBits},
                    {"ssra", static_cast<int>(topp->ssra_o) & 15, ssraBits},
                    {"slt", static_cast<int>(topp->slt_o), sv < 0},
                    {"vadd", static_cast<int>(topp->vadd_o), (uu + ww) & 0xff},
                    {"vmul", static_cast<int>(topp->vmul_o), uu * ww},
                    {"vdiv", static_cast<int>(topp->vdiv_o), (uu / ww) & 0xf},
                    {"vmod", static_cast<int>(topp->vmod_o), uu % ww},
                    {"vlt", static_cast<int>(topp->vlt_o), uu < ww},
                    {"slt_w", static_cast<int>(topp->slt_w_o), sv < ww},
                    {"sadd", static_cast<int>(topp->sadd_o) & 0xff, (sv + ww) & 0xff},
                    {"lit_t", static_cast<int>(topp->lit_t_o), 1},
                    {"lit_f", static_cast<int>(topp->lit_f_o), 0},
                    {"rol", static_cast<int>(topp->rol_o), ((uu << 1) | (uu >> 3)) & 15},
                    {"ror", static_cast<int>(topp->ror_o), ((uu >> 2) | (uu << 2)) & 15},
                    {"plus", static_cast<int>(topp->plus_o), a},
                    {"cond", static_cast<int>(topp->cond_o), uu & 1},
                    {"pkgconst", static_cast<int>(topp->pkgconst_o), 10},
                    {"slow", static_cast<int>(topp->slow_o), -2},
                    {"shigh", static_cast<int>(topp->shigh_o), 3},
                    {"sleft", static_cast<int>(topp->sleft_o), -2},
                    {"sright", static_cast<int>(topp->sright_o), 3},
                };
                for (const Check& check : checks) {
                    if (check.got != check.want) {
                        std::printf("%%Error: a=%d b=%d u=%d %s=%d expected %d\n", a, b, u,
                                    check.name, check.got, check.want);
                        ++fails;
                    }
                }
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
