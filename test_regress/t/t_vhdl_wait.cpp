// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL wait-until clock process test
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
    const auto checkAll = [&](int qp, int qf, int qe, int qb, int qv) {
        check("qp", static_cast<int>(topp->qp), qp);
        check("qf", static_cast<int>(topp->qf), qf);
        check("qe", static_cast<int>(topp->qe), qe);
        check("qb", static_cast<int>(topp->qb), qb);
        check("qv", static_cast<int>(topp->qv), qv);
    };

    topp->clk = 0;
    topp->d = 0;
    topp->eval();
    checkAll(0, 0, 0, 0, 0);

    // Rising-edge waits resume, the falling-edge wait does not
    topp->d = 1;
    topp->clk = 1;
    topp->eval();
    checkAll(1, 0, 1, 0, 1);

    // A falling edge resumes only its wait; the assignment after the body loops back to
    // the pre-wait assignment, so qb stays low
    topp->d = 0;
    topp->clk = 0;
    topp->eval();
    checkAll(1, 0, 1, 0, 1);

    // The falling edge sees the new data
    topp->d = 1;
    topp->clk = 1;
    topp->eval();
    topp->clk = 0;
    topp->eval();
    checkAll(1, 1, 1, 0, 1);

    // Rising edges update the rising-edge targets only
    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    checkAll(0, 1, 0, 0, 0);

    if (!fails) std::printf("%%Info: all checks passed\n");
    topp->final();
    delete topp;
    return fails ? 1 : 0;
}
