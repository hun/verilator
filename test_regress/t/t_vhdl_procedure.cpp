// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL procedure calls with in/out/inout parameters test
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static int fails = 0;

static void check(const char* name, int got, int want) {
    if (got != want) {
        std::printf("%%Error: %s got %d expected %d\n", name, got, want);
        ++fails;
    }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int acc = 0;
    int cnt = 0;
    int sa = 0;
    int pv = 0;
    topp->eval();
    for (int cycle = 0; cycle < 6; ++cycle) {
        const int a = cycle == 4 ? 6 : cycle;
        const int b = cycle == 4 ? 6 : 4;
        const int sel = cycle % 3;
        topp->clk = 0;
        topp->a = a;
        topp->b = b;
        topp->sel = sel;
        topp->eval();
        // Concurrent procedure call: pass(a, sc) drives q = a + 1
        check("q", static_cast<int>(topp->q), a + 1);

        topp->clk = 1;
        topp->eval();
        acc += 1;
        const int t = a + b;
        const int r = sel == 0 ? t : sel == 1 ? a - b : t * 2;
        if (t > 10) {
            acc += t;
        } else {
            acc += 1;  // Nested incr(acc) in calc
            acc += 1;  // acc := acc + 1 in calc
        }
        acc += 1;  // incr(acc), default argument
        acc += 2;  // incr(by => 2, v => acc), named association
        const int cWant = cnt;
        cnt += 1;  // bump(cnt, 1) is a non-blocking signal assignment
        const int n = sel + 1;
        const int sumWant = 6;  // sumto's static for loop 1 to 3
        sa += n + 1;  // sumto's local initializer and inout accumulator
        check("y", static_cast<int>(topp->y), r);
        check("z", static_cast<int>(topp->z), acc);
        check("c", static_cast<int>(topp->c), cWant);
        check("w", static_cast<int>(topp->w), sumWant);
        check("v", static_cast<int>(topp->v), sa);
        pv += sel + 1;  // Process-local procedure capturing pv
        check("u", static_cast<int>(topp->u), 3 + 3 * sel);  // outer's nested f()/inner()
        check("r2", static_cast<int>(topp->r2), pv);
        // Sibling procedure sb must see the process sq, not sa's local sq
        check("s", static_cast<int>(topp->s), 0);
        const int crWant = a >= 0 && a <= 2 ? 10 : a >= 3 && a <= 5 ? 20 : 30;
        const int cyWant = a >= 0 && a <= 2 ? 1 : a >= 3 && a <= 5 ? 2 : 3;
        check("cr", static_cast<int>(topp->cr), crWant);
        check("cy", static_cast<int>(topp->cy), cyWant);
        topp->clk = 0;
        topp->eval();
    }

    topp->final();
    delete topp;

    if (fails) return 1;
    std::printf("*-* All Finished *-*\n");
    return 0;
}
