// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL frontend lowering: shared value types
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
#ifndef VERILATOR_V3VHDLNATIVETYPES_H_
#define VERILATOR_V3VHDLNATIVETYPES_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3VhdlNativeLower.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vhdlnative {

struct VType;
using VTypeP = std::shared_ptr<VType>;

//! One record element: its bit offset from the least significant bit of the record
struct VField final {
    std::string name;  // Folded
    int offset = 0;
    int width = 1;
    VTypeP type;
};

//! VHDL type as it survives into the committed AST
struct VType final {
    enum Kind : uint8_t {
        LOGIC, BIT, BOOLEAN, INT, VECTOR, ARRAY, RECORD, ENUM, REAL, FILE, LINE
    } kind = LOGIC;
    AstNodeDType* dtp = nullptr;
    int width = 1;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    std::string typeName;  // Base type identity for overload resolution
    int left = 0;
    int right = 0;
    bool ascending = false;
    int intLow = 0;  // INTEGER subtype bounds
    int intHigh = 0;
    VTypeP elemp;  // ARRAY: element type
    int elemWidth = 1;  // ARRAY: width of one element
    std::vector<VField> fields;  // RECORD: most significant element first
    std::vector<std::string> literals;  // ENUM: literal names in declaration order

    bool isVector() const { return kind == VECTOR || kind == ARRAY; }
    bool isInt() const { return kind == INT; }
    bool isSigned() const { return signing == VSigning::SIGNED; }
    //! A VHDL array type, as opposed to a numeric_std unsigned/signed vector
    bool isVhdlArray() const {
        return signing == VSigning::NOSIGN && (kind == VECTOR || kind == ARRAY);
    }
    //! A predefined 1-D array type such as std_logic_vector or bit_vector (or a
    //! user-declared array of a one-bit element, which compares the same way)
    bool isPredefinedVector() const {
        return (kind == VECTOR && signing == VSigning::NOSIGN)
               || (kind == ARRAY && signing == VSigning::NOSIGN && elemWidth == 1);
    }
    //! Number of elements of an array, or 1 for a scalar
    int length() const {
        if (!isVector()) return 1;
        // An ascending range that ends below its start (or a descending one that ends
        // above it) has no elements: IEEE 1076-2008 5.2.1 calls it a null range
        if (ascending ? right < left : right > left) return 0;
        return std::abs(left - right) + 1;
    }
    //! Bit offset of a declared array index from the least significant element;
    //! the right bound is the least significant element in either direction
    int indexOffset(int index) const { return ascending ? right - index : index - right; }
};

bool baseTypesRelated(const std::string& a, const std::string& b);
bool arrayBaseTypesRelated(const std::string& a, const std::string& b);
bool literalIntegerExpr(const Expr& expr);
bool discreteSelectorType(const VType& type);
bool charLiteralFits(const VType& target, char ch);

}  // namespace vhdlnative
#endif  // Guard
