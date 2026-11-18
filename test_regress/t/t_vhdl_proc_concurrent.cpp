// -*- mode: C++; c-file-style: "cc-mode" //
//
// DESCRIPTION: Verilator: a concurrent procedure drives signal out parameters as one process
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
    const auto check = [&](unsigned a, unsigned b, unsigned sub, unsigned cin) {
        topp->a = a;
        topp->b = b;
        topp->sub = sub;
        topp->cin = cin;
        topp->eval();
        const unsigned r = a + (sub ? ((~b) & 0xff) : b) + cin;
        const unsigned wantQ = r & 0xff;
        const unsigned wantC = (r >> 8) & 1;
        if (topp->q != wantQ || topp->c != wantC) {
            std::printf("%%Error: a=%02x b=%02x sub=%u cin=%u q=%02x c=%u expected q=%02x c=%u\n",
                        a, b, sub, cin, static_cast<unsigned>(topp->q),
                        static_cast<unsigned>(topp->c), wantQ, wantC);
            ++fails;
        }
    };

    check(0x12, 0x34, 0, 0);
    check(0x12, 0x34, 1, 0);
    check(0x12, 0x34, 1, 1);
    check(0xff, 0x02, 0, 0);
    check(0xff, 0xff, 0, 1);
    check(0x00, 0x01, 1, 1);
    check(0xaa, 0x55, 0, 0);
    check(0xaa, 0x55, 1, 0);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
