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

#include "V3Ast.h"
#include "V3VhdlNativeLower.h"

#include <memory>
#include <string>
#include <vector>

namespace vhdlnative {

struct VType;
struct ViewInfo;
using VTypeP = std::shared_ptr<VType>;

//! One record element: its bit offset from the least significant bit of the record
struct VField final {
    std::string name;  // Folded
    int offset = 0;  // Bit offset of the element
    int width = 1;  // Bit width of the element
    VTypeP type;  // Subtype of the element
};

//! VHDL type as it survives into the committed AST
struct VType final {
    enum Kind : uint8_t {
        LOGIC, BIT, BOOLEAN, INT, VECTOR, ARRAY, RECORD, ENUM, REAL, FILE, LINE
    } kind = LOGIC;
    AstNodeDType* dtp = nullptr;  // AST data type of the committed type
    int width = 1;  // Bit width of the type
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;  // Keyword of the basic AST data type
    VSigning signing = VSigning::UNSIGNED;  // Signedness of the basic AST data type
    std::string typeName;  // Base type identity for overload resolution
    int left = 0;  // Index range left bound
    int right = 0;  // Index range right bound
    bool ascending = false;  // Index range is `to`, not `downto`
    bool unconstrainedBase = false;  // ARRAY: the base type's index range is open, as in `<>`
    int intLow = 0;  // INTEGER subtype bounds
    int intHigh = 0;  // Upper bound of the INTEGER subtype
    VTypeP elemp;  // ARRAY: element type
    int elemWidth = 1;  // ARRAY: width of one element
    std::vector<VField> fields;  // RECORD: most significant element first
    std::vector<std::string> literals;  // ENUM: literal names in declaration order
    // A mode view declaration: the record is this type and viewp says how each element is driven
    std::shared_ptr<const ViewInfo> viewp;

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

//! An interface view of a record: one leaf for each scalar-mode element, however deep it is
//! nested in views of records and arrays of records. A leaf is a port of its own.
struct ViewInfo final {
    //! One step from the viewed object to a leaf: a record element or an array index
    struct Step final {
        std::string name;  // Folded element name; unused for an index
        bool isIndex = false;
        int64_t index = 0;
    };
    struct Leaf final {
        std::vector<Step> path;  // From the viewed object down to the leaf
        VDirection dir;  // Direction seen from the entity that declares the port
        int offset = 0;  // Bit offset of the leaf in the viewed object
        int width = 1;  // Bit width of the leaf
        AstNodeDType* dtp = nullptr;  // Data type of the leaf, null when it is unconstrained
    };
    std::vector<Leaf> leaves;  // In the order the view declares them
    //! The view seen from the other end of the connection
    ViewInfo converse() const {
        ViewInfo out = *this;
        for (Leaf& leaf : out.leaves) {
            if (leaf.dir.isInput()) {
                leaf.dir = VDirection::OUTPUT;
            } else if (leaf.dir.isOutput()) {
                leaf.dir = VDirection::INPUT;
            }
        }
        return out;
    }
};

//! Code of a control character named by an enumeration literal of the predefined
//! `character` type (IEEE 1076-2008 16.3), or -1 when the lower-cased name is not one
int controlCharacterCode(const std::string& name);
bool baseTypesRelated(const std::string& a, const std::string& b);
bool arrayBaseTypesRelated(const std::string& a, const std::string& b);
bool literalIntegerExpr(const Expr& expr);
bool discreteSelectorType(const VType& type);
bool charLiteralFits(const VType& target, char ch);

}  // namespace vhdlnative
#endif  // Guard
