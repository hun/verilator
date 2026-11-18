// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL a report message of only logic characters still reports
//
// This file ONLY is placed in the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    // The severity failure reports and ends the run, so the assignment after
    // the report never happens
    topp->eval();
    if (topp->done_o != 0) {
        std::printf("%%Error: done_o=%d exp=0\n", topp->done_o);
        ++fails;
    }
    if (!Verilated::gotFinish()) {
        std::printf("%%Error: the report did not end the run\n");
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
