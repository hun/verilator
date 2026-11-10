// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL shared variable test
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
    const auto edge = [&]() {
        topp->clk = 1;
        topp->eval();
        topp->clk = 0;
        topp->eval();
    };

    topp->clk = 0;
    topp->a = 0;
    topp->we = 0;
    topp->waddr = 0;
    topp->raddr = 0;
    topp->din = 0;
    topp->eval();

    // acc is a shared variable with an initializer; q follows it after each edge
    topp->a = 5;
    topp->eval();
    check("mix", static_cast<int>(topp->mix), 5);
    edge();
    check("q", static_cast<int>(topp->q), 5);
    // process(all) is not sensitive to the shared variable changing
    check("mix", static_cast<int>(topp->mix), 5);
    topp->a = 3;
    topp->eval();
    check("mix", static_cast<int>(topp->mix), 6);
    edge();
    check("q", static_cast<int>(topp->q), 8);
    check("etag", static_cast<int>(topp->etag), 0x2a);
    check("bcount", static_cast<int>(topp->bcount), 3);

    // Shared memory: write in one process, read from another
    topp->we = 1;
    topp->waddr = 1;
    topp->din = 0x11;
    edge();
    topp->we = 0;
    topp->raddr = 1;
    edge();
    check("dout", static_cast<int>(topp->dout), 0x11);

    topp->we = 1;
    topp->waddr = 2;
    topp->din = 0x22;
    edge();
    topp->we = 0;
    topp->raddr = 2;
    edge();
    check("dout", static_cast<int>(topp->dout), 0x22);

    topp->raddr = 1;
    edge();
    check("dout", static_cast<int>(topp->dout), 0x11);

    if (!fails) std::printf("%%Info: all checks passed\n");
    topp->final();
    delete topp;
    return fails ? 1 : 0;
}
