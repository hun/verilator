// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL enumeration types and attributes test
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
    const int expect[4] = {0, 1, 2, 0};  // idle, run, done, idle
    const int expectConstPos = 4;  // pos(run) + pos(run) + pos(done)

    topp->clk = 0;
    topp->sel = 0;
    topp->eval();
    for (int step = 0; step < 4; ++step) {
        if (step) {
            topp->clk = 1;
            topp->eval();
        }
        const int state = static_cast<int>(topp->state_o);
        if (state != expect[step]) {
            std::printf("%%Error: step %d state=%d expected %d\n", step, state, expect[step]);
            ++fails;
        }
        if (static_cast<int>(topp->q) != (state == 0)) {
            std::printf("%%Error: step %d q=%d state=%d\n", step,
                        static_cast<int>(topp->q), state);
            ++fails;
        }
        if (static_cast<int>(topp->pos_o) != state) {
            std::printf("%%Error: step %d pos_o=%d state=%d\n", step,
                        static_cast<int>(topp->pos_o), state);
            ++fails;
        }
        if (static_cast<int>(topp->high_o) != (state == 2)) {
            std::printf("%%Error: step %d high_o=%d state=%d\n", step,
                        static_cast<int>(topp->high_o), state);
            ++fails;
        }
        if (static_cast<int>(topp->low_o) != (state == 0)) {
            std::printf("%%Error: step %d low_o=%d state=%d\n", step,
                        static_cast<int>(topp->low_o), state);
            ++fails;
        }
        if (static_cast<int>(topp->const_pos_o) != expectConstPos) {
            std::printf("%%Error: step %d const_pos_o=%d expected %d\n", step,
                        static_cast<int>(topp->const_pos_o), expectConstPos);
            ++fails;
        }
        // 'succ is only defined below the last enumeration value
        if (state < 2 && static_cast<int>(topp->succ_o) != state + 1) {
            std::printf("%%Error: step %d succ_o=%d state=%d\n", step,
                        static_cast<int>(topp->succ_o), state);
            ++fails;
        }
        for (int sel = 0; sel < 4; ++sel) {
            topp->sel = sel;
            topp->eval();
            if (static_cast<int>(topp->val_o) != sel) {
                std::printf("%%Error: sel=%d val_o=%d\n", sel, static_cast<int>(topp->val_o));
                ++fails;
            }
        }
        if (step) {
            topp->clk = 0;
            topp->eval();
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
