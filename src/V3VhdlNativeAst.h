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
// SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
//
// Frontend interface types shared by the native VHDL stages:
//   lexer (V3VhdlNativeLexer.cpp) -> parser (V3VhdlNativeParse.cpp) ->
//   elaboration and lowering (V3VhdlNativeLower.cpp).
//
//*************************************************************************

#ifndef VERILATOR_V3VHDLNATIVEAST_H_
#define VERILATOR_V3VHDLNATIVEAST_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3FileLine.h"
#include "V3VhdlNative.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

//######################################################################
// Tokens and lexer

//! Map one VHDL logic value character to the 2-state model: '0', '1', 'x', 'z', or 0
char vhdlLogicBit(char c);

//! The VHDL reserved words, IEEE 1076-2008 15.10
bool isReservedWord(const std::string& folded);

//! Report a language construct outside the implemented subset at its source location
void unsupportedError(FileLine* flp, const std::string& what);

struct Token final {
    enum Kind : uint8_t { ID, INT, REAL, CHAR, BITS, STRING, SYM, END } kind = END;
    std::string text;  // ID: folded identifier. SYM: operator. INT/REAL: spelling
    std::string spelling;  // ID: identifier as written
    std::string bits;  // BITS: one character ('0'/'1'/'x'/'z') per bit, msb first
    char sym = 0;  // SYM: first character
    int64_t intValue = 0;  // INT
    double realValue = 0;  // REAL
    char charValue = 0;  // CHAR
    FileLine* flp = nullptr;  // Source location
};

class Lexer final {
    const std::string m_filename;  // File being lexed
    const std::string m_text;  // File contents
    size_t m_pos = 0;  // Current offset
    int m_line = 1;  // Current line number
    size_t m_lineStart = 0;  // Offset of the current line's first character
    bool m_prevEndsDesignator = false;  // Last token can precede an attribute/qualification mark
    bool m_prevId = false;  // The last token was an identifier: `mark'(` needs one
    std::string m_prevText;  // The last identifier, to tell a mark from a keyword
    int m_translateOff = 0;  // Comment pragma translate_off regions still open

    FileLine* newFileLine() const {
        FileLine* const flp = new FileLine{m_filename};
        flp->lineno(m_line);
        const int column = static_cast<int>(m_pos - m_lineStart) + 1;
        flp->column(column, column);
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
    Token lexBits(FileLine* flp, const std::string& baseText, int length = -1);
    int pragmaDirection(size_t start, size_t end) const;  // Comment pragma in a source span
    void notePragma(int direction);
    Token scanToken();  // One token, translate_off regions included
    Token nextRaw();  // One token, before the designator-context update
    void skipDigits();  // Consume a run of digits and underscores
    bool consumeExponent();  // Consume an exponent with digits, if present
    Token lexIdentifier(FileLine* flp);
    Token lexExtendedIdentifier(FileLine* flp);
    Token lexNumber(FileLine* flp);
    Token lexIntegerLiteral(FileLine* flp, const std::string& first);
    Token lexBasedLiteral(FileLine* flp, const std::string& first);
    Token makeRealToken(FileLine* flp, size_t start);
    Token lexCharOrMark(FileLine* flp);
    Token lexStringLiteral(FileLine* flp);

public:
    Lexer(std::string filename, std::string text)
        : m_filename{std::move(filename)}
        , m_text{std::move(text)} {}
    ~Lexer() = default;

    //! Save/restore point for one-token lookahead
    struct State final {
        size_t pos;  // Offset of the next character
        int line;  // Current line number
        size_t lineStart;  // Offset of the current line's first character
        bool prevEndsDesignator;  // Last token can precede an attribute/qualification mark
        bool prevId;  // The last token was an identifier
        std::string prevText;  // The last identifier, to tell a mark from a keyword
        int translateOff;  // Comment pragma translate_off regions still open
    };
    State state() const {
        return {m_pos,    m_line,     m_lineStart,   m_prevEndsDesignator,
                m_prevId, m_prevText, m_translateOff};
    }
    void restore(const State& st) {
        m_pos = st.pos;
        m_line = st.line;
        m_lineStart = st.lineStart;
        m_prevEndsDesignator = st.prevEndsDesignator;
        m_prevId = st.prevId;
        m_prevText = st.prevText;
        m_translateOff = st.translateOff;
    }

    Token next();
};

//######################################################################
// Frontend syntax tree

//! Port mode as written in a port clause
enum class PortMode : uint8_t { IN, OUT, INOUT };

struct Expr;
struct TypeSpec;
using ExprP = std::unique_ptr<Expr>;

struct Expr final {
    enum Kind : uint8_t {
        INT_LIT,  // intValue
        REAL_LIT,  // realValue
        PHYS_LIT,  // text/spelling: unit name, realValue: the abstract literal unscaled
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
        CALL,  // Subprogram call: text=folded name (a=prefix for a qualified name),
               // args=actuals, formalNames=associated formal of each actual
        SLICE,  // a=prefix, b=left, c=right, text="downto"/"to"
        ATTR,  // a=prefix, text=attribute name, args=optional attribute parameter
        QUAL,  // a=operand, text=type mark of a type qualification
        SELECTED,  // a=prefix, text=selected name (record element or package member)
        AGG_OTHERS,  // a=value
        AGG_POSITIONAL,  // args=element values, leftmost index first
        AGG_NAMED,  // args=values, choices=one choice list per value
        UNAFFECTED,  // `unaffected`: a waveform that leaves the assignment target unchanged
        // External name `<< class path : subtype >>`: text=class, a=path as a chain of
        // SELECTED names, intValue=`^` levels up (-1 when the path is absolute),
        // typeSpec=subtype
        EXTERNAL,
    } kind;
    FileLine* flp = nullptr;  // Source location
    std::string text;  // Name/operator, folded
    std::string spelling;  // Name as written
    int64_t intValue = 0;  // INT_LIT value
    double realValue = 0;  // REAL_LIT value; PHYS_LIT: the abstract literal, unscaled
    char charValue = 0;  // CHAR_LIT value
    std::string bits;  // BITS_LIT: one character per bit, msb first
    ExprP a, b, c;  // Operands and prefixes; roles depend on kind
    std::vector<ExprP> args;  // Arguments and elements; roles depend on kind
    // AGG_NAMED: the choices associated with each element value; an empty list marks a
    // positional association
    std::vector<std::vector<ExprP>> choices;
    // CALL: the formal associated with each actual; empty for a positional actual
    std::vector<std::string> formalNames;
    std::shared_ptr<const TypeSpec> typeSpec;  // EXTERNAL: the subtype; shared by copies

    explicit Expr(Kind kind_)
        : kind{kind_} {}
};

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

//! Deep copy of an expression
inline ExprP cloneExpr(const Expr& expr) {
    ExprP out = std::make_unique<Expr>(expr.kind);
    out->flp = expr.flp;
    out->text = expr.text;
    out->spelling = expr.spelling;
    out->intValue = expr.intValue;
    out->realValue = expr.realValue;
    out->charValue = expr.charValue;
    out->bits = expr.bits;
    out->formalNames = expr.formalNames;
    out->typeSpec = expr.typeSpec;
    out->a = expr.a ? cloneExpr(*expr.a) : ExprP{};
    out->b = expr.b ? cloneExpr(*expr.b) : ExprP{};
    out->c = expr.c ? cloneExpr(*expr.c) : ExprP{};
    for (const ExprP& argp : expr.args) out->args.push_back(argp ? cloneExpr(*argp) : ExprP{});
    for (const std::vector<ExprP>& choiceList : expr.choices) {
        std::vector<ExprP> copy;
        for (const ExprP& choicep : choiceList) {
            copy.push_back(choicep ? cloneExpr(*choicep) : ExprP{});
        }
        out->choices.push_back(std::move(copy));
    }
    return out;
}

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
        ASSERT,  // `assert`; the condition is value, the message is message
        RETURN,  // Subprogram return; value is the function result, null for a procedure
        CALL,  // Procedure call statement; value is the call
    } kind;
    FileLine* flp = nullptr;  // Source location
    ExprP target;  // ASSIGN/VAR_ASSIGN
    //! ASSIGN: `force` writes value over the drivers, `release` (value is `unaffected`) ends it
    enum Force : uint8_t { FORCE_NONE, FORCE_SET, FORCE_RELEASE } force = FORCE_NONE;
    ExprP value;  // ASSIGN/VAR_ASSIGN/RETURN; CALL: the call expression
    // ASSIGN/VAR_ASSIGN: a conditional waveform `v1 when c1 else ... vN`; empty for a
    // plain assignment. (value, condition); a null condition marks the trailing else.
    std::vector<std::pair<ExprP, ExprP>> alternatives;
    // IF: (condition, body); a null condition marks the final else
    std::vector<std::pair<ExprP, std::vector<StmtP>>> branches;
    ExprP selector;  // CASE: the case expression
    // CASE: (choices, body); an empty choice list marks "others"
    std::vector<std::pair<std::vector<ExprP>, std::vector<StmtP>>> caseItems;
    // WAIT: value holds the `until` condition; an empty one suspends forever
    std::vector<std::string> waitNames;  // WAIT: `on` sensitivity names
    bool waitForever = false;  // WAIT without `until` or `on`
    // WAIT: `for <delay>`
    ExprP delay;  // WAIT: `for <delay>`; ASSIGN: `after <time>`, only zero is a delta
    ExprP message;  // ASSERT: the `report` text, null for the default
    //! Severity of a report or an assert, which decides whether the run continues
    enum Severity : uint8_t { SEV_NOTE, SEV_WARNING, SEV_ERROR, SEV_FAILURE } severity = SEV_NOTE;
    // FOR: loop variable over b..c, ascending when rangeAscending
    std::string loopVar;
    bool rangeAscending = true;  // FOR: true for an ascending range
    std::string loopLabel;  // Label of the loop, empty when unlabeled
    ExprP b, c;  // FOR: range bounds b..c, in written order
    std::vector<StmtP> body;  // Loop body statements

    explicit Stmt(Kind kind_)
        : kind{kind_} {}
};

struct TypeSpec final {
    std::string mark;  // Folded type mark
    std::string pkg;  // Package qualifier of a `work.pkg.mark` reference, if any
    std::string resolution;  // Folded name of a resolution function written before the mark
    std::string objectName;  // A `name'subtype` indication: the mark above is an object
    bool elementMark = false;  // The indication is `name'element`: an array's element type
    FileLine* flp = nullptr;  // Source location
    bool hasRange = false;  // A range constraint is present, bounded by left and right
    bool ascending = false;  // "to", else "downto"
    std::string constraintMark;  // Index constraint that names a subtype, e.g. mark(constraint)
    ExprP left, right;  // Range bounds, in written order; see hasRange
    // A record subtype constraint names a discrete range per member: rec_t(x(3 downto 0), ...)
    struct MemberConstraint final {
        std::string name;  // Folded member name
        bool ascending = false;  // "to", else "downto"
        ExprP lo, hi;  // Bounds in written order, e.g. 3 downto 0 -> lo=3, hi=0
        ExprP range;  // A range attribute instead: rec_t(x(sig'range)), IEEE 1076-2008 6.3
        // Constraints of the member's elements, outermost first: rec_t(x(0 to 3)(y(7 downto 0)))
        std::vector<TypeSpec> elementConstraints;
        bool open = false;  // `(open)`: the member's own range is left to its use
        // Constraints of the members of a record-typed member: rec_t(r(f(7 downto 0)))
        std::vector<MemberConstraint> nested;
    };
    // Per-member discrete ranges of a record subtype constraint
    std::vector<MemberConstraint> memberConstraints;
    // A nested-array subtype indication `t(r1)(r2, ...)` beyond the first index range
    // (which lives in this spec's own hasRange/left/right), outermost first, innermost last
    std::vector<TypeSpec> indexConstraints;
};

struct PortDecl final {
    FileLine* flp = nullptr;  // Source location
    std::vector<std::pair<std::string, std::string>> names;  // (folded, spelling)
    PortMode dir = PortMode::IN;  // Port mode
    TypeSpec type;  // Subtype indication; of a view port, the `of` record type if written
    ExprP init;  // Default value for an unassociated input
    bool isView = false;  // VHDL-2019 interface view port: `view V` replaces the mode
    TypeSpec viewMark;  // The view's name, see isView
    bool viewConverse = false;  // The view is written `V'converse`
    bool viewArray = false;  // `view (V) of array_type`: an array of views
};

struct Decl final {
    enum Kind : uint8_t { SIGNAL, CONSTANT, VARIABLE, FILE } kind;
    FileLine* flp = nullptr;  // Source location
    // Declared names as (folded, spelling)
    std::vector<std::pair<std::string, std::string>> names;
    TypeSpec type;  // Subtype indication of the declared objects
    ExprP init;  // Initial value, if any
    std::string fileMode;  // FILE: the open mode
    explicit Decl(Kind kind_)
        : kind{kind_} {}
};

//! One entity generic: a constant with an optional default value
struct GenericDecl final {
    FileLine* flp = nullptr;  // Source location
    bool isType = false;  // A generic type parameter; type and init are unused
    bool isSubprogram = false;  // A generic subprogram parameter; only names is modelled
    // Declared names as (folded, spelling)
    std::vector<std::pair<std::string, std::string>> names;
    TypeSpec type;  // Subtype indication
    ExprP init;  // Default value, if any
};

//! `alias name [: subtype_mark] is designator [ signature ];` renames an existing
//! object, slice or subprogram
struct AliasDecl final {
    FileLine* flp = nullptr;  // Source location
    std::string name;  // Folded
    std::string spelling;  // As written
    bool hasSignature = false;  // `[ ... ]`: the designator denotes a subprogram
    TypeSpec mark;  // Optional subtype indication, which fixes the alias's index bounds
    ExprP designator;  // The aliased object, slice or subprogram
};

//! A type, subtype or alias declaration
struct TypeDecl final {
    enum Kind : uint8_t { ENUM, ARRAY, RECORD, SUBTYPE, PHYSICAL, VIEW } kind;
    FileLine* flp = nullptr;  // Source location
    std::string name;  // Folded
    std::string spelling;  // As written
    std::vector<std::pair<std::string, std::string>> literals;  // ENUM: (folded, spelling)
    TypeSpec base;  // SUBTYPE: base mark and optional constraint; ARRAY: element type
    TypeSpec index;  // ARRAY: index constraint
    // ARRAY: further index constraints of a multi-dimensional type `array (i1, i2, ...)`,
    // outermost first; `index` holds the first
    std::vector<TypeSpec> indexes;
    struct Member final {
        // Field names as (folded, spelling)
        std::vector<std::pair<std::string, std::string>> names;
        TypeSpec type;  // Field subtype indication; VIEW: the view a nested element names
        PortMode mode = PortMode::IN;  // VIEW: mode of the element, unless it names a view
        bool isView = false;  // VIEW: the element is itself a view named by type
        bool isArray = false;  // VIEW: `view (v)`, an array of views
        bool converse = false;  // VIEW: the nested view is written `v'converse`
    };
    std::vector<Member> members;  // RECORD; VIEW: the elements, with base the record type

    // PHYSICAL: one `unit_name [= <numeric> <unit_name>]` line; the value names an earlier
    // unit of the same type, or a predefined unit of `time'
    struct Unit final {
        std::string name;  // Folded
        std::string spelling;  // As written
        ExprP value;  // Scale: a physical literal in an earlier unit; null for the base unit
    };
    std::vector<Unit> units;  // PHYSICAL, in declaration order; the first sets the step unit

    explicit TypeDecl(Kind kind_)
        : kind{kind_} {}
};

//! One formal parameter of a subprogram
struct FormalDecl final {
    enum Class : uint8_t { CONSTANT, VARIABLE, SIGNAL } cls = CONSTANT;
    FileLine* flp = nullptr;  // Source location
    // Declared names as (folded, spelling)
    std::vector<std::pair<std::string, std::string>> names;
    PortMode mode = PortMode::IN;  // Parameter mode
    TypeSpec type;  // No range and no constraint mark: unconstrained
    ExprP init;  // Default actual
};

//! A function or procedure declaration; `hasBody` is false for a package prototype, whose
//! body arrives with the package body
struct SubprogramDecl final {
    enum Kind : uint8_t { FUNCTION, PROCEDURE } kind;
    bool impure = false;  // `impure` prefix; no subprogram may read a signal yet
    bool hasBody = false;  // False for a package prototype with no body yet
    FileLine* flp = nullptr;  // Source location
    std::string unitName;  // The package that declares it, if any: a body sees that package
    std::string name;  // Folded
    std::string spelling;  // As written
    std::vector<FormalDecl> formals;  // Formal parameters
    TypeSpec returnType;  // FUNCTION
    std::vector<TypeDecl> types;  // Declarative part of the body
    std::vector<Decl> decls;  // Object declarations of the body
    std::vector<AliasDecl> aliases;  // Alias declarations of the body
    std::vector<SubprogramDecl> subprograms;  // Nested declarations, visible in the body
    std::vector<StmtP> body;  // Statements of the body

    explicit SubprogramDecl(Kind kind_)
        : kind{kind_} {}
};

struct Process final {
    FileLine* flp = nullptr;  // Source location
    bool hasSensitivity = false;  // Sensitivity list is present, including process(all)
    bool sensitivityAll = false;  // process(all): every signal the body reads
    std::vector<std::string> sensitivity;  // Folded names
    // Element designators (`rec.field`, `v(i)`, `v(hi downto lo)`): each denotes a
    // subelement signal of the process's sensitivity list (IEEE 1076-2008 11.3)
    std::vector<ExprP> sensitivityElems;
    std::vector<Decl> decls;  // Variables of the process declarative part
    std::vector<TypeDecl> types;  // Types declared by the process
    std::vector<SubprogramDecl> subprograms;  // Procedures and functions of the process
    std::vector<AliasDecl> aliases;  // Aliases of the process declarative part
    std::vector<StmtP> body;  // Sequential statements of the process
};

//! One `formal => actual` of a port map; an `open` actual has no expression
struct PortAssoc final {
    FileLine* flp = nullptr;  // Source location
    std::string formal;  // Folded
    std::string formalSpelling;  // As written
    // A formal naming one element of a record port: `rec.field => ...`
    std::string formalMember;  // Folded
    std::string formalMemberSpelling;  // As written
    // A formal that names part of a port: `a(7 downto 4) => ...`
    bool hasFormalRange = false;
    bool formalAscending = true;  // "to", else "downto"
    ExprP formalLeft, formalRight;  // Range bounds of a partial formal; see hasFormalRange
    ExprP actual;  // Null for `open`
};

struct Concurrent;

//! A component declaration: the interface a component instantiation binds to
struct ComponentDecl final {
    FileLine* flp = nullptr;  // Source location
    std::string name;  // Folded
    std::string spelling;  // As written
    std::vector<GenericDecl> generics;  // Generic interface
    std::vector<PortDecl> ports;  // Port interface
};

//! One branch of a conditional generate. A null condition marks the final else
//! part, which is taken only when no preceding condition is true.
struct GenBranch final {
    ExprP cond;  // Condition of this branch; null for the final else
    std::vector<Decl> decls;  // The branch's own declarative items
    std::vector<TypeDecl> types;  // Type declarations of the branch
    std::vector<SubprogramDecl> subprograms;  // Subprogram declarations of the branch
    std::vector<ComponentDecl> components;  // Component declarations of the branch
    std::vector<std::unique_ptr<Concurrent>> stmts;  // Concurrent statements of the branch
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
        ASSERT,  // `assert`; the condition is target, the message is value
        CALL,  // Concurrent procedure call; value is the call
    } kind;
    //! Severity of a concurrent assertion, which decides whether the run continues
    Stmt::Severity severity = Stmt::SEV_NOTE;
    FileLine* flp = nullptr;  // Source location
    ExprP target;  // ASSIGN/COND_ASSIGN/SEL_ASSIGN
    ExprP value;  // ASSIGN/SEL_ASSIGN
    // COND_ASSIGN: (value, condition); a null condition marks the final else
    std::vector<std::pair<ExprP, ExprP>> alternatives;
    ExprP selector;  // SEL_ASSIGN
    // SEL_ASSIGN: (choices, value); an empty choice list marks "others"
    std::vector<std::pair<std::vector<ExprP>, ExprP>> choices;
    std::unique_ptr<Process> process;  // PROCESS
    // GEN_FOR: loop variable over lo..hi; BLOCK: plain statement list
    std::string loopVar;
    bool rangeAscending = true;  // GEN_FOR: true for an ascending range
    ExprP lo, hi;  // GEN_FOR: range bounds lo..hi
    // INSTANCE: the instantiated entity and its port associations
    std::string instanceLabel;
    std::string instanceName;  // Folded entity or component name
    // INSTANCE: the selected architecture, empty when none or `-` names any
    std::string architectureName;
    // True for a direct `entity work.X`; false when the instantiation names a component
    bool entityInstantiation = false;
    // True for `label : configuration work.X`: instanceName holds X until the configuration
    // names the entity and its architecture
    bool configurationInstantiation = false;
    // ASSIGN/COND_ASSIGN: `after <delay>`
    ExprP delay;
    // ASSIGN: further waveform elements of a list, each with its own `after` delay
    std::vector<std::pair<ExprP, ExprP>> waveform;
    std::vector<PortAssoc> associations;  // INSTANCE: port map associations
    // INSTANCE: the `generic map` associations, formal names the generic constant
    std::vector<PortAssoc> genericAssociations;
    std::vector<Decl> decls;  // BLOCK: declarative items
    std::vector<AliasDecl> aliases;  // BLOCK: object aliases
    std::vector<TypeDecl> types;  // BLOCK/GEN_FOR
    std::vector<SubprogramDecl> subprograms;  // BLOCK/GEN_FOR
    std::vector<ComponentDecl> components;  // BLOCK/GEN_FOR
    std::vector<std::unique_ptr<Concurrent>> body;  // GEN_FOR/BLOCK: statements
    std::vector<GenBranch> branches;  // GEN_IF: in source order
    explicit Concurrent(Kind kind_)
        : kind{kind_} {}
    Concurrent(const Concurrent&) = delete;
    Concurrent& operator=(const Concurrent&) = delete;
};

//! One use clause: `use [lib.]pkg.all` or `use [lib.]pkg.decl`, which makes one
//! declaration visible rather than the whole package (IEEE 1076-2008 12.4)
struct UseClause final {
    std::string lib;  // Not modelled beyond the record
    std::string pkg;  // Folded package name
    std::string decl;  // The one declaration named; empty for `.all`
};

struct EntityUnit final {
    std::string name;  // Folded
    std::string spelling;  // As written
    std::string libname;  // Library name the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
    FileLine* flp = nullptr;  // Source location
    std::vector<PortDecl> ports;  // Port clause
    // Entity declarative items, visible in every architecture of the entity
    std::vector<Decl> decls;
    std::vector<TypeDecl> types;  // Type declarations of the entity
    // Packages named by the enclosing context clause
    std::vector<UseClause> uses;
    // Context declarations named by the enclosing context clause
    std::vector<std::string> contextNames;
    // Generic constants, visible in the port clause and every architecture
    std::vector<GenericDecl> generics;
    std::vector<SubprogramDecl> subprograms;  // Subprogram declarations of the entity
    std::vector<ComponentDecl> components;  // Component declarations of the entity
    // The entity statement part: concurrent assertions and passive processes
    std::vector<std::unique_ptr<Concurrent>> stmts;
};

struct Architecture final {
    std::string name;  // Folded
    std::string entityName;  // Folded
    std::string libname;  // Library name the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
    FileLine* flp = nullptr;  // Source location
    std::vector<Decl> decls;  // Object declarations of the architecture
    std::vector<TypeDecl> types;  // Type declarations of the architecture
    std::vector<AliasDecl> aliases;  // Alias declarations of the architecture
    std::vector<SubprogramDecl> subprograms;  // Subprogram declarations of the architecture
    std::vector<ComponentDecl> components;  // Component declarations of the architecture
    std::vector<std::unique_ptr<Concurrent>> stmts;  // Concurrent statements of the architecture
    std::vector<UseClause> uses;  // Use clauses of the context clause
    std::vector<std::string> contextNames;  // Context declarations named by the context clause
};

//! A package declaration or body; its declarative part is elaborated once
struct PackageUnit final {
    std::string name;  // Folded
    std::string spelling;  // As written
    std::string libname;  // Library name the file was requested in
    std::vector<GenericDecl> generics;  // Generic parameters: the package is a template
    bool inLibrary = false;  // File came from a -v/--library file
    bool isBody = false;  // Package body: completes the constants declared by the spec
    FileLine* flp = nullptr;  // Source location
    std::vector<Decl> decls;  // Object declarations of the package
    std::vector<TypeDecl> types;  // Type declarations of the package
    std::vector<AliasDecl> aliases;  // Alias declarations of the package
    // Use clauses made visible around the package
    std::vector<UseClause> uses;
    std::vector<std::string> contextNames;  // Context declarations named by the context clause
    std::vector<SubprogramDecl> subprograms;  // Prototypes in a spec, bodies in a body
    std::vector<ComponentDecl> components;  // Component declarations of the package
};
//! `package NAME is new BASE [generic map (...)];` instantiates a generic package
struct PackageInstance final {
    std::string name;  // Folded
    std::string spelling;  // As written
    std::string libname;  // Library name the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
    FileLine* flp = nullptr;  // Source location
    std::string baseName;  // Folded generic package being instantiated
    std::vector<PortAssoc> genericAssociations;  // Generic map associations
    std::vector<UseClause> uses;  // Use clauses of the context clause
    std::vector<std::string> contextNames;  // Context declarations named by the context clause
};

//! One component binding of a configuration: `for <instances> : <comp> use entity <lib>.<ent>;`
struct ConfigBinding final {
    std::string instances;  // Folded instance labels, comma separated as written
    bool all = false;  // `all` instead of a label list
    bool others = false;  // `others` instead of a label list
    std::string component;  // Folded component name
    std::string architecture;  // Folded architecture the `use entity` clause selects, or empty
    std::string libname;  // Library named by the `use entity` clause
    std::string entity;  // Folded entity name the `use entity` clause binds
    std::vector<PortAssoc> genericMap;  // `generic map (...)` of the binding indication
    FileLine* flp = nullptr;  // Source location
};

//! A configuration design unit: binds the component specifications of one architecture
struct ConfigurationUnit final {
    std::string name;  // Folded
    std::string spelling;  // As written
    std::string libname;  // Library name the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
    FileLine* flp = nullptr;  // Source location
    std::string entityName;  // Folded entity the configuration configures
    std::string archName;  // Folded architecture of that entity
    std::vector<ConfigBinding> bindings;  // Component bindings
    std::vector<std::string> usedConfigurations;  // Folded names of `use configuration`
};

//! A context declaration: a named bundle of library and use clauses
struct ContextUnit final {
    std::string name;  // Folded
    FileLine* flp = nullptr;  // Source location
    std::vector<UseClause> uses;  // Use clauses of the context
    std::vector<std::string> contextNames;  // Contexts this one references
};

//! Design units parsed from all source files, in source order
struct DesignLibrary final {
    std::vector<EntityUnit> entities;  // Entity units
    std::vector<Architecture> archs;  // Architecture units
    std::vector<PackageUnit> packages;  // Specs and bodies, in source order
    std::vector<PackageInstance> packageInstances;  // Generic-package instances
    std::vector<ConfigurationUnit> configurations;  // Configuration units
    std::vector<ContextUnit> contexts;  // Context declarations, in source order
};

//! Parse one design file and append its design units to the library
void parseFile(const V3VhdlNativeFile& file, DesignLibrary& library) VL_MT_DISABLED;

}  // namespace vhdlnative

#endif  // Guard
