// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: an array local of an inlined function
//
// This file ONLY is placed under the Creative Commons Public Domain.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: CC0-1.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdint>
#include <cstdio>

namespace {
uint32_t heavy(uint32_t v) {
    uint32_t a[2] = {v & 0xffu, 0};
    for (int i = 0; i < 32; ++i) {
        const uint32_t src0 = (a[0] >> i) & 1u;
        const uint32_t src1 = (a[0] >> ((i / 2 + 1) % 32)) & 1u;
        a[1] = (a[1] & ~(1u << i)) | ((src0 | src1) << i);
        const uint32_t b1 = (a[1] >> i) & 1u;
        const uint32_t b0 = (a[0] >> ((i + 3) % 32)) & 1u;
        a[0] = (a[0] & ~(1u << i)) | ((b1 ^ b0) << i);
    }
    return a[1];
}
}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    int fails = 0;
    for (unsigned v = 0; v < 256; ++v) {
        topp->i = v;
        topp->eval();
        const uint32_t want = heavy(v);
        if (static_cast<uint32_t>(topp->o) != want) {
            if (fails < 8) {
                std::printf("%%Error: i=%02x o=%08x expected %08x\n", v,
                            static_cast<unsigned>(topp->o), want);
            }
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
