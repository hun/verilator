// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL physical types with user-declared units
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
    topp->eval();

    int fails = 0;
    const int got[11] = {static_cast<int>(topp->q_step),   static_cast<int>(topp->q_gen_k),
                         static_cast<int>(topp->q_gen_hz), static_cast<int>(topp->q_baud),
                         static_cast<int>(topp->q_sum),    static_cast<int>(topp->q_div),
                         static_cast<int>(topp->q_frac),   static_cast<int>(topp->q_kib),
                         static_cast<int>(topp->q_mib),    static_cast<int>(topp->q_mib3),
                         static_cast<int>(topp->q_mbd)};
    const int exp[11]
        = {1000000, 100000, 100000000, 115200,  2500000, 500,
           1500,    1024,   1048576,   3145728, 1000000};  // 1 MHz, 100 MHz, 100 MHz, 115200 kBd,
                                                           // 2 MHz + 500 kHz, 1 MHz, 1.5 kHz, 1
                                                           // MiB, 1 MiB, 3 MiB and 1 MBd, each
                                                           // divided by one unit of its type
    const char* const name[11] = {"q_step", "q_gen_k", "q_gen_hz", "q_baud", "q_sum", "q_div",
                                  "q_frac", "q_kib",   "q_mib",    "q_mib3", "q_mbd"};
    for (int i = 0; i < 11; ++i) {
        if (got[i] != exp[i]) {
            std::printf("%%Error: %s=%d exp=%d\n", name[i], got[i], exp[i]);
            ++fails;
        }
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
