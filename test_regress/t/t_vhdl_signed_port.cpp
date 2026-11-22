// -*- mode: C++; c-file-style: "cc-mode" -*-
//
// DESCRIPTION: Verilator: VHDL numeric_std signed ports of several widths
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3,
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

#include <verilated.h>

#include VM_PREFIX_INCLUDE

#include <cstdint>
#include <cstdio>

// A signed port is an unsigned field of two's complement bits: sign-extend it here
static int64_t signExtend(uint64_t value, int bits) {
    const uint64_t sign = 1ULL << (bits - 1);
    const uint64_t mask = (sign << 1) - 1;
    return static_cast<int64_t>(((value & mask) ^ sign) - sign);
}

static int fails = 0;

static void check(const char* name, int64_t a, int64_t b, int64_t got, int64_t expected) {
    if (got != expected) {
        std::printf("%%Error: a=%lld b=%lld %s=%lld exp=%lld\n", static_cast<long long>(a),
                    static_cast<long long>(b), name, static_cast<long long>(got),
                    static_cast<long long>(expected));
        ++fails;
    }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    VM_PREFIX* const topp = new VM_PREFIX;

    const int64_t cases[][2] = {{-100, 27}, {5, -3}, {-128, 127}, {-1, -1}, {0, 0}, {127, -128}};
    for (const auto& vec : cases) {
        const int64_t a = vec[0];
        const int64_t b = vec[1];
        // Drive a signed port with the low bits of the two's complement value
        topp->a = static_cast<uint8_t>(a & 0xff);
        topp->b = static_cast<uint8_t>(b & 0xff);
        topp->eval();
        check("sum", a, b, signExtend(topp->sum, 9), a + b);
        check("neg", a, b, topp->neg, a < 0);
        check("lt", a, b, topp->lt, a < b);
        check("sh", a, b, signExtend(topp->sh, 8), a >> 2);
        check("ab", a, b, signExtend(topp->ab, 8),
              signExtend(static_cast<uint64_t>(a < 0 ? -a : a), 8));
        check("ext", a, b, signExtend(topp->ext, 16), a);
        check("wide", a, b, signExtend(topp->wide, 40), a);
        check("i", a, b, static_cast<int32_t>(topp->i), a);
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
