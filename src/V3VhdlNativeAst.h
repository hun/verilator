// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: tokens and syntax tree
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
// Frontend interface types shared by the native VHDL stages:
//   lexer (V3VhdlNativeLexer.cpp) -> parser (V3VhdlNativeParse.cpp) ->
//   semantic analysis (V3VhdlNativeSem.cpp) -> elaboration
//   (V3VhdlNativeElab.cpp) -> lowering (V3VhdlNativeLower.cpp).
//
//*************************************************************************

#ifndef VERILATOR_V3VHDLNATIVE_AST_H_
#define VERILATOR_V3VHDLNATIVE_AST_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3FileLine.h"
#include "V3VhdlNative.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

//######################################################################
// Tokens and lexer

//! Map one VHDL logic value character to the 2-state model: '0', '1', 'x', 'z', or 0
char vhdlLogicBit(char c);

//! Report a language construct outside the implemented subset at its source location
void unsupportedError(FileLine* flp, const std::string& what);

struct Token final {
    enum Kind : uint8_t { ID, INT, CHAR, BITS, STRING, SYM, END } kind = END;
    std::string text;  // ID: folded identifier. SYM: operator. INT: spelling
    std::string spelling;  // ID: identifier as written
    std::string bits;  // BITS: one character ('0'/'1'/'x'/'z') per bit, msb first
    char sym = 0;  // SYM: first character
    int64_t intValue = 0;  // INT
    char charValue = 0;  // CHAR
    FileLine* flp = nullptr;
};

class Lexer final {
    const std::string m_filename;  // File being lexed
    const std::string m_text;  // File contents
    size_t m_pos = 0;  // Current offset
    int m_line = 1;  // Current line number

    FileLine* newFileLine() const {
        FileLine* const flp = new FileLine{m_filename};
        flp->lineno(m_line);
        flp->applyIgnores();  // Respect .vlt lint waivers
        return flp;
    }
    char peek(size_t ahead) const {
        return m_pos + ahead < m_text.size() ? m_text[m_pos + ahead] : '\0';
    }
    static bool isIdStart(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    static bool isIdChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }
    static int bitsPerDigit(int base) { return base == 2 ? 1 : base == 8 ? 3 : 4; }
    static bool bitsFromDigits(const std::string& digits, int base, std::string& out);
    static bool decimalValue(const std::string& digits, int64_t& out);

    void skipSpace();
    Token lexSymbol(FileLine* flp);
    Token lexBits(FileLine* flp, const std::string& baseText);

public:
    Lexer(std::string filename, std::string text)
        : m_filename{std::move(filename)}
        , m_text{std::move(text)} {}
    ~Lexer() = default;

    //! Save/restore point for one-token lookahead
    std::pair<size_t, int> state() const { return {m_pos, m_line}; }
    void restore(const std::pair<size_t, int>& st) {
        m_pos = st.first;
        m_line = st.second;
    }

    Token next();
};

//######################################################################
// Frontend syntax tree

//! Port mode as written in a port clause
enum class PortMode : uint8_t { IN, OUT, INOUT };

struct Expr;
using ExprP = std::unique_ptr<Expr>;

struct Expr final {
    enum Kind : uint8_t {
        INT_LIT,  // intValue
        BITS_LIT,  // bits
        STRING_LIT,  // spelling: a string value that is not a bit-vector literal
        CHAR_LIT,  // charValue
        NAME,  // text/spelling
        TRUE_LIT,
        FALSE_LIT,
        PAREN,  // a
        UNARY,  // text=operator, a
        BINARY,  // text=operator, a, b
        INDEX,  // a=prefix, args
        SLICE,  // a=prefix, b=left, c=right, text="downto"/"to"
        ATTR,  // a=prefix, text=attribute name, args=optional attribute parameter
        QUAL,  // a=operand, text=type mark of a type qualification
        SELECTED,  // a=prefix, text=selected name (record element or package member)
        AGG_OTHERS,  // a=value
        AGG_POSITIONAL,  // args=element values, leftmost index first
        AGG_NAMED,  // args=values, choices=one choice list per value
    } kind;
    FileLine* flp = nullptr;
    std::string text;  // Name/operator, folded
    std::string spelling;  // Name as written
    int64_t intValue = 0;
    char charValue = 0;
    std::string bits;
    ExprP a, b, c;
    std::vector<ExprP> args;
    // AGG_NAMED: the choices associated with each element value; an empty list marks a
    // positional association
    std::vector<std::vector<ExprP>> choices;

    explicit Expr(Kind kind_)
        : kind{kind_} {}
};

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

struct Stmt final {
    enum Kind : uint8_t {
        ASSIGN,
        VAR_ASSIGN,
        IF,
        CASE,
        FOR,
        WHILE,
        LOOP,
        EXIT,
        NEXT,
        WAIT,
        REPORT,
    } kind;
    FileLine* flp = nullptr;
    ExprP target;  // ASSIGN/VAR_ASSIGN
    ExprP value;  // ASSIGN/VAR_ASSIGN
    // IF: (condition, body); a null condition marks the final else
    std::vector<std::pair<ExprP, std::vector<StmtP>>> branches;
    ExprP selector;  // CASE: the case expression
    // CASE: (choices, body); an empty choice list marks "others"
    std::vector<std::pair<std::vector<ExprP>, std::vector<StmtP>>> caseItems;
    // WAIT: value holds the `until` condition; an empty one suspends forever
    std::vector<std::string> waitNames;  // WAIT: `on` sensitivity names
    bool waitForever = false;  // WAIT without `until` or `on`
    // FOR: loop variable over b..c, ascending when rangeAscending
    std::string loopVar;
    bool rangeAscending = true;
    std::string loopLabel;
    ExprP b, c;
    std::vector<StmtP> body;

    explicit Stmt(Kind kind_)
        : kind{kind_} {}
};

struct TypeSpec final {
    std::string mark;  // Folded type mark
    FileLine* flp = nullptr;
    bool hasRange = false;
    bool ascending = false;  // "to", else "downto"
    std::string constraintMark;  // Index constraint that names a subtype, e.g. mark(constraint)
    ExprP left, right;
};

struct PortDecl final {
    FileLine* flp = nullptr;
    std::vector<std::pair<std::string, std::string>> names;  // (folded, spelling)
    PortMode dir = PortMode::IN;
    TypeSpec type;
};

struct Decl final {
    enum Kind : uint8_t { SIGNAL, CONSTANT, VARIABLE } kind;
    FileLine* flp = nullptr;
    std::vector<std::pair<std::string, std::string>> names;
    TypeSpec type;
    ExprP init;
    explicit Decl(Kind kind_)
        : kind{kind_} {}
};

//! `alias name [: subtype_mark] is designator;` renames an existing object
struct AliasDecl final {
    FileLine* flp = nullptr;
    std::string name;  // Folded
    std::string spelling;
    TypeSpec mark;  // Optional subtype indication, which fixes the alias's index bounds
    ExprP designator;  // The aliased object or slice
};

//! A type, subtype or alias declaration
struct TypeDecl final {
    enum Kind : uint8_t { ENUM, ARRAY, RECORD, SUBTYPE } kind;
    FileLine* flp = nullptr;
    std::string name;  // Folded
    std::string spelling;
    std::vector<std::pair<std::string, std::string>> literals;  // ENUM: (folded, spelling)
    TypeSpec base;  // SUBTYPE: base mark and optional constraint; ARRAY: element type
    TypeSpec index;  // ARRAY: index constraint
    struct Member final {
        std::vector<std::pair<std::string, std::string>> names;
        TypeSpec type;
    };
    std::vector<Member> members;  // RECORD

    explicit TypeDecl(Kind kind_)
        : kind{kind_} {}
};

struct Process final {
    FileLine* flp = nullptr;
    bool hasSensitivity = false;
    bool sensitivityAll = false;  // process(all): every signal the body reads
    std::vector<std::string> sensitivity;  // Folded names
    std::vector<Decl> decls;  // Variables of the process declarative part
    std::vector<TypeDecl> types;  // Types declared by the process
    std::vector<StmtP> body;
};

//! One `formal => actual` of a port map; an `open` actual has no expression
struct PortAssoc final {
    FileLine* flp = nullptr;
    std::string formal;  // Folded
    std::string formalSpelling;
    // A formal that names part of a port: `a(7 downto 4) => ...`
    bool hasFormalRange = false;
    bool formalAscending = true;  // "to", else "downto"
    ExprP formalLeft, formalRight;
    ExprP actual;  // Null for `open`
};

struct Concurrent final {
    enum Kind : uint8_t {
        ASSIGN,
        COND_ASSIGN,
        SEL_ASSIGN,
        PROCESS,
        GEN_FOR,
        GEN_IF,
        BLOCK,
        INSTANCE,
    } kind;
    FileLine* flp = nullptr;
    ExprP target;  // ASSIGN/COND_ASSIGN/SEL_ASSIGN
    ExprP value;  // ASSIGN/SEL_ASSIGN
    // COND_ASSIGN: (value, condition); a null condition marks the final else
    std::vector<std::pair<ExprP, ExprP>> alternatives;
    ExprP selector;  // SEL_ASSIGN
    // SEL_ASSIGN: (choices, value); an empty choice list marks "others"
    std::vector<std::pair<std::vector<ExprP>, ExprP>> choices;
    std::unique_ptr<Process> process;  // PROCESS
    // GEN_FOR: loop variable over lo..hi; GEN_IF: cond; BLOCK: plain statement list
    std::string loopVar;
    bool rangeAscending = true;
    ExprP lo, hi;
    ExprP cond;
    // INSTANCE: the instantiated entity and its port associations
    std::string instanceLabel;
    std::string instanceName;  // Folded entity or component name
    std::vector<PortAssoc> associations;
    std::vector<Decl> decls;  // BLOCK: declarative items
    std::vector<TypeDecl> types;  // BLOCK
    std::vector<std::unique_ptr<Concurrent>> body;  // GEN_*/BLOCK: statements
    std::vector<std::unique_ptr<Concurrent>> elseBody;  // GEN_IF: else part
    explicit Concurrent(Kind kind_)
        : kind{kind_} {}
    Concurrent(const Concurrent&) = delete;
    Concurrent& operator=(const Concurrent&) = delete;
};

struct EntityUnit final {
    std::string name;  // Folded
    std::string spelling;
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    std::vector<PortDecl> ports;
    // Entity declarative items, visible in every architecture of the entity
    std::vector<Decl> decls;
    std::vector<TypeDecl> types;
    // Packages named by the enclosing context clause: (library, package)
    std::vector<std::pair<std::string, std::string>> uses;
};

struct Architecture final {
    std::string name;  // Folded
    std::string entityName;  // Folded
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    std::vector<Decl> decls;
    std::vector<TypeDecl> types;
    std::vector<AliasDecl> aliases;
    std::vector<std::string> components;  // Declared component names, folded
    std::vector<std::unique_ptr<Concurrent>> stmts;
    std::vector<std::pair<std::string, std::string>> uses;
};

//! A package declaration or body; its declarative part is elaborated once
struct PackageUnit final {
    std::string name;  // Folded
    std::string spelling;
    std::string libname;
    bool inLibrary = false;
    bool isBody = false;  // Package body: completes the constants declared by the spec
    FileLine* flp = nullptr;
    std::vector<Decl> decls;
    std::vector<TypeDecl> types;
    std::vector<AliasDecl> aliases;
};

//! Design units parsed from all source files, in source order
struct DesignLibrary final {
    std::vector<EntityUnit> entities;
    std::vector<Architecture> archs;
    std::vector<PackageUnit> packages;  // Specs and bodies, in source order
};

//! Parse one design file and append its design units to the library
void parseFile(const V3VhdlNativeFile& file, DesignLibrary& library) VL_MT_DISABLED;

}  // namespace vhdlnative

#endif  // Guard
