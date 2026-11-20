// -*- mode: C++; c-file-style: "cc-mode" //
//
// DESCRIPTION: Verilator: a element of a signal in a sensitivity list
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    topp->rec_a = 0;
    topp->rec_b = 0;
    topp->v = 0;
    topp->eval();

    int fails = 0;
    const auto checkCount = [&](const char* what, unsigned want) {
        topp->eval();
        const unsigned got = topp->n;
        if (got != want) {
            std::printf("%%Error: %s: activations=%u expected %u\n", what, got, want);
            ++fails;
        }
    };

    // VHDL runs the process once at time zero
    checkCount("after time zero", 1);

    // Another element of the same record does not activate the process
    topp->rec_b = 1;
    checkCount("after rec.b", 1);

    // The sensitive element does
    topp->rec_a = 0x0a;
    checkCount("after rec.a", 2);
    if (topp->y != 0x0a) {
        std::printf("%%Error: y=%x expected a\n", static_cast<unsigned>(topp->y));
        ++fails;
    }

    // Only v(0) is sensitive, not the rest of the vector
    topp->v = 0x80;
    checkCount("after v(7)", 2);
    topp->v = 0x81;
    checkCount("after v(0)", 3);
    if (topp->y != 0x05) {
        std::printf("%%Error: y=%x expected 5\n", static_cast<unsigned>(topp->y));
        ++fails;
    }

    // A slice element activates on any bit of the slice
    topp->rec_a = 0x03;
    checkCount("after rec.a again", 4);

    topp->final();
    delete topp;

    if (fails) {
        std::printf("%%Error: %d failures\n", fails);
        return 1;
    }
    std::printf("*-* All Finished *-*\n");
    return 0;
}
