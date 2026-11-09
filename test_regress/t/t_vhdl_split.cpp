// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL process splitting test
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
    const auto check = [&](const char* name, int got, int want) {
        if (got != want) {
            std::printf("%%Error: %s=%d expected %d\n", name, got, want);
            ++fails;
        }
    };
    struct Vals final {
        int last, comb, q1, q2, q3, qv1, qv2, qe, qm, qr, qr2;
    };
    const auto checkAll = [&](const Vals& want) {
        check("last", static_cast<int>(topp->last), want.last);
        check("comb", static_cast<int>(topp->comb), want.comb);
        check("q1", static_cast<int>(topp->q1), want.q1);
        check("q2", static_cast<int>(topp->q2), want.q2);
        check("q3", static_cast<int>(topp->q3), want.q3);
        check("qv1", static_cast<int>(topp->qv1), want.qv1);
        check("qv2", static_cast<int>(topp->qv2), want.qv2);
        check("qe", static_cast<int>(topp->qe), want.qe);
        check("qm", static_cast<int>(topp->qm), want.qm);
        check("qr", static_cast<int>(topp->qr), want.qr);
        check("qr2", static_cast<int>(topp->qr2), want.qr2);
    };

    topp->clk = 0;
    topp->clk2 = 0;
    topp->d = 0;
    topp->a = 0;
    topp->en = 0;
    topp->eval();
    checkAll({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    // The unclocked statement follows its own sensitivity
    topp->a = 1;
    topp->eval();
    checkAll({0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    // The last of two same-edge assignments wins
    topp->d = 1;
    topp->en = 1;
    topp->clk = 1;
    topp->eval();
    checkAll({1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0});

    // The falling edge runs the later branches of a single-clock chain
    topp->clk = 0;
    topp->eval();
    checkAll({1, 1, 1, 1, 0, 1, 0, 0, 1, 1, 0});

    // The second clock drives only its own targets and variables
    topp->d = 0;
    topp->clk2 = 1;
    topp->eval();
    checkAll({1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0});

    // A later rising edge on the first clock updates only its own targets
    topp->clk = 1;
    topp->eval();
    checkAll({0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0});

    if (!fails) std::printf("%%Info: all checks passed\n");
    topp->final();
    delete topp;
    return fails ? 1 : 0;
}
