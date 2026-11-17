// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an asynchronous reset flip-flop clears without a clock edge
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

static VM_PREFIX* topp;
static int fails = 0;

static void check(unsigned want, const char* what) {
    if (topp->cnt_o == want) return;
    std::printf("%%Error: %s: cnt=%u exp=%u\n", what, static_cast<unsigned>(topp->cnt_o), want);
    ++fails;
}

static void tick() {
    topp->clk = 1;
    topp->eval();
    topp->clk = 0;
    topp->eval();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    topp = new VM_PREFIX;
    topp->clk = 0;
    topp->rstn = 0;
    topp->eval();

    // The reset applies with no clock edge at all
    check(0, "in reset");
    topp->rstn = 1;
    for (int i = 0; i < 5; ++i) tick();
    check(5, "after five clocks");

    // It also applies between edges, which a level test alone would miss
    topp->rstn = 0;
    topp->eval();
    check(0, "reset between edges");
    topp->rstn = 1;
    for (int i = 0; i < 3; ++i) tick();
    check(3, "after three more clocks");

    // A falling clock edge is not a clock edge here
    topp->clk = 1;
    topp->eval();
    topp->clk = 0;
    topp->eval();
    check(4, "after another clock");

    topp->final();
    delete topp;
    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
