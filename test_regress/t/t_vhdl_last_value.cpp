// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL last_value of a signal that has an event
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

    int fails = 0;
    topp->din = 0;
    topp->eval();
    unsigned prev = 0;
    unsigned falls = 0;
    bool fell = false;
    bool rose = false;
    // Pulses of different lengths, with repeated values that are not events
    const unsigned pattern[] = {1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0};
    for (const unsigned value : pattern) {
        topp->din = value;
        topp->eval();
        // The process runs only on an event, so the outputs hold until the next one
        if (value != prev) {
            fell = prev == 1 && value == 0;
            rose = prev == 0 && value == 1;
            if (fell) ++falls;
        }
        if (topp->fall != fell || topp->rise != rose || topp->cnt != falls) {
            if (++fails < 10) {
                std::printf("%%Error: din=%u fall=%u/%u rise=%u/%u cnt=%u/%u\n", value,
                            topp->fall, fell, topp->rise, rose, topp->cnt, falls);
            }
        }
        prev = value;
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
