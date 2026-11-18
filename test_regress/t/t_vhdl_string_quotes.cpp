// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: a doubled quote inside a string literal
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
    // "a ""b"" c" is seven characters: a, space, quote, b, quote, space, c
    if (topp->len != 7) {
        std::printf("%%Error: len=%u expected 7\n", static_cast<unsigned>(topp->len));
        ++fails;
    }
    // The quote character is code 34, and "a""" is two characters
    if (topp->inner != 34 + 34 + 2) {
        std::printf("%%Error: inner=%u expected 70\n", static_cast<unsigned>(topp->inner));
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
