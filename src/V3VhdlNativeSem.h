// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: semantic analysis
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of either the GNU Lesser General Public License Version 3
// or the Perl Artistic License Version 2.0.
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
//
// Result of semantic analysis: every name resolved to a Symbol, every
// expression typed, and every statically known operand folded to a value.
// Elaboration (V3VhdlNativeElab) and lowering (V3VhdlNativeLower) consume
// this tree; no name lookup happens after analysis.
//
//*************************************************************************

#ifndef VERILATOR_V3VHDLNATIVE_SEM_H_
#define VERILATOR_V3VHDLNATIVE_SEM_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3VhdlNativeAst.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

//######################################################################
// Types

//! An analyzed VHDL type
struct SemType final {
    enum Kind : uint8_t {
        VOID,  // Not yet analyzed
        LOGIC,  // std_logic/std_ulogic
        BIT,  // bit
        BOOLEAN,
        INTEGER,
        REAL,
        ENUM,
        VECTOR,  // One-dimensional array of a scalar element type
        ARRAY,  // One-dimensional array of a non-scalar element type
        RECORD,
    } kind = VOID;
    bool ascending = false;  // Index direction: "to" when true, "downto" when false
    int left = 0;  // Left index bound
    int right = 0;  // Right index bound
    bool isSigned = false;  // Signed vector (signed, or an integer range)
    int intLeft = 0;  // INTEGER: subtype low bound
    int intRight = 0;  // INTEGER: subtype high bound
    const SemType* elemp = nullptr;  // VECTOR/ARRAY element type

    bool isVector() const { return kind == VECTOR || kind == ARRAY; }
    bool isScalar() const {
        return kind == LOGIC || kind == BIT || kind == BOOLEAN || kind == INTEGER || kind == REAL;
    }
    bool isLogicFamily() const { return kind == LOGIC || kind == BIT; }
    //! Number of elements of an array type, or 1 for a scalar
    int width() const { return isVector() ? std::abs(left - right) + 1 : 1; }
    //! Index of the least significant element
    int lowIndex() const { return ascending ? left : right; }
    //! Map a declared index to a bit offset from the least significant element
    int offsetOf(int index) const { return ascending ? index - left : left - index; }
};

//! Interned VHDL types
class SemTypes final {
    std::map<uint8_t, const SemType*> m_basics;  // Basic type per kind
    std::map<std::string, std::unique_ptr<SemType>> m_byKey;  // Interned compound types

    const SemType* intern(const std::string& key, const SemType& type);

public:
    SemTypes() = default;
    ~SemTypes() = default;
    SemTypes(const SemTypes&) = delete;
    SemTypes& operator=(const SemTypes&) = delete;

    //! Basic type of one kind (LOGIC, BIT, BOOLEAN, INTEGER, REAL)
    const SemType* basic(SemType::Kind kind);
    //! Integer subtype over the given inclusive range
    const SemType* integerRange(int low, int high);
    //! Array of elemp with the given index range
    const SemType* vector(const SemType* elemp, int left, int right, bool ascending,
                          bool isSigned);
};

//######################################################################
// Static values

//! A value known at analysis time
struct SemValue final {
    enum Kind : uint8_t {
        NONE,  // Not a static value
        INT,
        BOOL,
        LOGIC,  // One logic value in bits[0]: '0', '1', 'x' or 'z'
        BITS,  // One character per bit, most significant first
        STRING,  // One character per byte, leftmost first
        REAL,
    } kind = NONE;
    int64_t intValue = 0;
    double realValue = 0;
    std::string bits;  // LOGIC/BITS/STRING contents
};

//######################################################################
// Symbols

struct Symbol final {
    enum Kind : uint8_t {
        PORT,
        SIGNAL,
        CONSTANT,
        VARIABLE,
        ENUM_LITERAL,
    } kind = SIGNAL;
    std::string name;  // Folded
    std::string spelling;  // As written
    FileLine* flp = nullptr;
    const SemType* typep = nullptr;
    PortMode mode = PortMode::IN;  // PORT
    bool hasValue = false;  // CONSTANT: a static value is available
    SemValue value;
    int enumValue = 0;  // ENUM_LITERAL: position in its type
    int pinNum = 0;  // PORT: order in the port clause, 1-based

    bool isWritable() const {
        return kind == SIGNAL || kind == VARIABLE || kind == PORT
               || (kind == PORT && mode != PortMode::IN);
    }
    bool isPort() const { return kind == PORT; }
};

//! A set of declared names with a parent scope
struct Scope final {
    std::map<std::string, Symbol*> names;  // Folded name -> symbol
    const Scope* parentp = nullptr;  // Enclosing scope

    Symbol* find(const std::string& name) const {
        for (const Scope* scopep = this; scopep; scopep = scopep->parentp) {
            const auto it = scopep->names.find(name);
            if (it != scopep->names.end()) return it->second;
        }
        return nullptr;
    }
};

//######################################################################
// Analyzed expressions and statements

struct SemExpr;
using SemExprP = std::unique_ptr<SemExpr>;

struct SemExpr final {
    enum Kind : uint8_t {
        INT_LIT,
        BITS_LIT,
        CHAR_LIT,
        STRING_LIT,
        TRUE_LIT,
        FALSE_LIT,
        NAME,  // symbolp
        UNARY,  // text=operator, args[0]
        BINARY,  // text=operator, args[0..1]
        CONVERSION,  // text=target, args[0] (and args[1] for to_unsigned/to_signed)
        EDGE,  // text=rising_edge/falling_edge, args[0]=signal
        INDEX,  // args[0]=prefix, args[1]=index
        SLICE,  // text=downto/to, args[0]=prefix, args[1]=left, args[2]=right
        AGG_OTHERS,  // args[0]=value
        AGG_POSITIONAL,  // args=elements
    } kind;
    FileLine* flp = nullptr;
    std::string text;  // Operator, conversion target or slice direction
    std::string spelling;  // NAME: as written
    int64_t intValue = 0;
    double realValue = 0;
    char charValue = 0;
    std::string bits;  // BITS_LIT/STRING_LIT contents
    const SemType* typep = nullptr;  // Analyzed type
    Symbol* symbolp = nullptr;  // NAME
    std::vector<SemExprP> args;
    bool hasValue = false;  // A static value was folded
    SemValue value;

    explicit SemExpr(Kind kind_)
        : kind{kind_} {}
};

struct SemStmt;
using SemStmtP = std::unique_ptr<SemStmt>;

struct SemStmt final {
    enum Kind : uint8_t { ASSIGN, IF, NULL_STMT } kind;
    FileLine* flp = nullptr;
    SemExprP target;  // ASSIGN
    SemExprP value;  // ASSIGN
    // IF: (condition, body); a null condition marks the final else
    std::vector<std::pair<SemExprP, std::vector<SemStmtP>>> branches;

    explicit SemStmt(Kind kind_)
        : kind{kind_} {}
};

struct SemProcess final {
    FileLine* flp = nullptr;
    bool hasSensitivity = false;
    std::vector<Symbol*> sensitivity;  // Signals in the sensitivity list
    std::vector<std::unique_ptr<Symbol>> decls;  // Process declarative part
    std::vector<SemStmtP> body;
};

struct SemConc final {
    enum Kind : uint8_t { ASSIGN, COND_ASSIGN, SEL_ASSIGN, PROCESS } kind;
    FileLine* flp = nullptr;
    SemExprP target;  // ASSIGN/COND_ASSIGN/SEL_ASSIGN
    SemExprP value;  // ASSIGN/SEL_ASSIGN
    // COND_ASSIGN: (value, condition); a null condition marks the final else
    std::vector<std::pair<SemExprP, SemExprP>> alternatives;
    SemExprP selector;  // SEL_ASSIGN
    // SEL_ASSIGN: (choices, value); an empty choice list marks "others"
    std::vector<std::pair<std::vector<SemExprP>, SemExprP>> choices;
    std::unique_ptr<SemProcess> process;  // PROCESS

    explicit SemConc(Kind kind_)
        : kind{kind_} {}
};

//######################################################################
// Analyzed design

struct SemEntity final {
    std::string name;  // Folded
    std::string spelling;
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    Scope scope;
    std::vector<std::unique_ptr<Symbol>> symbols;  // Owns the ports
    std::vector<Symbol*> ports;  // In port-clause order
};

struct SemArchitecture final {
    std::string name;  // Folded
    std::string entityName;  // Folded
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    SemEntity* entityp = nullptr;
    Scope scope;
    std::vector<std::unique_ptr<Symbol>> symbols;  // Owns the declarations
    std::vector<std::unique_ptr<SemConc>> stmts;
    bool analyzed = false;
};

//! Whole analyzed design
struct SemDesign final {
    SemTypes types;
    std::vector<std::unique_ptr<SemEntity>> entities;
    std::vector<std::unique_ptr<SemArchitecture>> archs;
};

//! Analyze all design units: resolve names, type expressions, fold statics
void analyze(DesignLibrary& library, SemDesign& design) VL_MT_DISABLED;

}  // namespace vhdlnative

#endif  // Guard
