// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL clock edge idiom test
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
    const auto checkAll = [&](int q, int qg, int qp, int qn, int qc, int qf, int qr, int qe) {
        check("q", static_cast<int>(topp->q), q);
        check("qg", static_cast<int>(topp->qg), qg);
        check("qp", static_cast<int>(topp->qp), qp);
        check("qn", static_cast<int>(topp->qn), qn);
        check("qc", static_cast<int>(topp->qc), qc);
        check("qf", static_cast<int>(topp->qf), qf);
        check("qr", static_cast<int>(topp->qr), qr);
        check("qe", static_cast<int>(topp->qe), qe);
    };

    // Initialize every register: a rising edge with d=0, then a falling edge
    topp->clk = 0;
    topp->rst = 0;
    topp->en = 1;
    topp->d = 0;
    topp->eval();
    topp->clk = 1;
    topp->eval();
    topp->clk = 0;
    topp->eval();
    checkAll(0, 0, 0, 0, 0, 0, 0, 0);

    // Rising edge with d=1 and en=1 loads every register
    topp->d = 1;
    topp->en = 1;
    topp->clk = 1;
    topp->eval();
    checkAll(1, 1, 1, 1, 1, 0, 1, 1);

    // Falling edge updates the falling-edge register only
    topp->clk = 0;
    topp->eval();
    checkAll(1, 1, 1, 1, 1, 1, 1, 1);

    // Enable low holds the gated registers
    topp->en = 0;
    topp->d = 0;
    topp->clk = 1;
    topp->eval();
    checkAll(0, 1, 1, 0, 0, 1, 0, 0);

    // Asynchronous reset takes effect without a clock edge
    topp->en = 1;
    topp->d = 1;
    topp->rst = 1;
    topp->clk = 0;
    topp->eval();
    checkAll(0, 1, 1, 0, 0, 1, 0, 0);

    // Release the reset and clock the next value in
    topp->rst = 0;
    topp->clk = 1;
    topp->eval();
    checkAll(1, 1, 1, 1, 1, 1, 1, 1);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
