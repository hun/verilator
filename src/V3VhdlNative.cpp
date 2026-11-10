// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend
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
// In-tree VHDL frontend. This replaces the GHDL XML importer with a hand-written
// lexer/parser and a lowerer that emits the committed (post-width) Verilator AST
// directly, so GHDL is not invoked.
//
// Structure: Lexer -> Parser (frontend syntax tree) -> Lowering (Verilator AST).
// The parse of every file completes before any lowering, because VHDL design
// units are order independent and an architecture may name an entity declared in
// a later file.
//
// Implemented subset (see vhdl_synth_subset.md for the target set):
//   - entity with ports; architecture with signal/constant declarations
//   - scalar std_logic/std_ulogic/bit/boolean, integer subtypes, constrained
//     std_logic_vector/std_ulogic_vector/bit_vector/unsigned/signed
//   - concurrent signal assignments
//   - processes with an explicit sensitivity list, if/elsif/else, signal
//     assignments, rising_edge/falling_edge clock idioms (including an
//     asynchronous reset branch)
//   - literals, names, indexing/slicing with a statically known width,
//     arithmetic/logic/relational/concat operators, vector conversions,
//     `(others => ...)` aggregates, static integer expressions
// Everything else reports `Unsupported:` with the VHDL source location.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNative.h"

#include "V3Error.h"
#include "V3File.h"
#include "V3FileLine.h"
#include "V3Global.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

VL_DEFINE_DEBUG_FUNCTIONS;

namespace {

//######################################################################
// Tokens and lexer

//! Map one VHDL logic value character to the 2-state model: '0', '1', 'x', 'z', or 0
VL_MT_SAFE static char vhdlLogicBit(char c) {
    switch (c) {
    case '0':
    case 'L':
    case 'l': return '0';
    case '1':
    case 'H':
    case 'h': return '1';
    case 'X':
    case 'x':
    case 'U':
    case 'u':
    case 'W':
    case 'w':
    case '-': return 'x';
    case 'Z':
    case 'z': return 'z';
    default: return 0;
    }
}

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
    //! Convert based-literal digits to a bit string, most significant bit first
    static bool bitsFromDigits(const std::string& digits, int base, std::string& out) {
        out.clear();
        for (const char c : digits) {
            if (c == '_') continue;
            const char bit = vhdlLogicBit(c);
            int value = -1;
            if (c >= '0' && c <= '9') {
                value = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                value = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                value = c - 'A' + 10;
            }
            if (bit && (value < 0 || value >= base)) {
                // 'X'/'Z'/'-' digits replicate the unknown/high-Z value
                for (int i = 0; i < bitsPerDigit(base); ++i) out.push_back(bit);
                continue;
            }
            if (value < 0 || value >= base) return false;
            for (int bitn = bitsPerDigit(base) - 1; bitn >= 0; --bitn) {
                out.push_back(((value >> bitn) & 1) ? '1' : '0');
            }
        }
        return true;
    }
    //! Convert a decimal digit string to an integer, skipping underscores
    static bool decimalValue(const std::string& digits, int64_t& out) {
        int64_t value = 0;
        bool any = false;
        for (const char c : digits) {
            if (c == '_') continue;
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            value = value * 10 + (c - '0');
            any = true;
        }
        out = value;
        return any;
    }

    void skipSpace() {
        for (;;) {
            if (m_pos >= m_text.size()) return;
            const char c = m_text[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                ++m_pos;
            } else if (c == '-' && peek(1) == '-') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
            } else if (c == '/' && peek(1) == '*') {
                m_pos += 2;
                while (m_pos < m_text.size() && !(m_text[m_pos] == '*' && peek(1) == '/')) {
                    if (m_text[m_pos] == '\n') ++m_line;
                    ++m_pos;
                }
                if (m_pos < m_text.size()) m_pos += 2;
            } else {
                return;
            }
        }
    }
    Token lexSymbol(FileLine* flp) {
        static const char* const threech[] = {"?/="};
        static const char* const twoch[]
            = {"<=", ">=", ":=", "=>", "/=", "**", "<>", "?=", "<<", ">>"};
        Token tok;
        tok.kind = Token::SYM;
        tok.flp = flp;
        for (const char* op : threech) {
            if (m_text.compare(m_pos, 3, op) == 0) {
                m_pos += 3;
                tok.text = op;
                tok.sym = op[0];
                return tok;
            }
        }
        for (const char* op : twoch) {
            if (m_text.compare(m_pos, 2, op) == 0) {
                m_pos += 2;
                tok.text = op;
                tok.sym = op[0];
                return tok;
            }
        }
        tok.sym = m_text[m_pos];
        tok.text = std::string(1, tok.sym);
        ++m_pos;
        return tok;
    }
    Token lexBits(FileLine* flp, const std::string& baseText) {
        // Caller has verified the next character is '"'
        const int base = baseText == "b" ? 2 : baseText == "o" ? 8 : baseText == "d" ? 10 : 16;
        ++m_pos;  // Opening quote
        std::string digits;
        while (m_pos < m_text.size() && m_text[m_pos] != '"' && m_text[m_pos] != '\n') {
            digits.push_back(m_text[m_pos]);
            ++m_pos;
        }
        Token tok;
        tok.kind = Token::BITS;
        tok.flp = flp;
        if (m_pos < m_text.size() && m_text[m_pos] == '"') {
            ++m_pos;
        } else {
            flp->v3error("VHDL bit-string literal is missing its closing '\"'");
            tok.kind = Token::END;
            return tok;
        }
        if (base == 10) {
            flp->v3error("Unsupported: VHDL decimal bit-string literal");
            tok.kind = Token::END;
            return tok;
        }
        if (!bitsFromDigits(digits, base, tok.bits)) {
            flp->v3error("Malformed VHDL bit-string literal");
            tok.kind = Token::END;
        }
        return tok;
    }

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

    Token next() {
        skipSpace();
        Token tok;
        tok.flp = newFileLine();
        if (m_pos >= m_text.size()) {
            tok.kind = Token::END;
            return tok;
        }

        const char c = m_text[m_pos];
        if (isIdStart(c)) {
            const size_t start = m_pos;
            while (m_pos < m_text.size() && isIdChar(m_text[m_pos])) ++m_pos;
            tok.kind = Token::ID;
            tok.spelling = m_text.substr(start, m_pos - start);
            tok.text = tok.spelling;
            // VHDL identifiers are case-insensitive; lookup uses the folded form
            std::transform(tok.text.begin(), tok.text.end(), tok.text.begin(), [](char ch) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            });
            // A one-letter base prefix directly followed by '"' is a bit-string literal
            if (tok.text.size() == 1
                && (tok.text == "b" || tok.text == "o" || tok.text == "x" || tok.text == "d")
                && peek(0) == '"') {
                return lexBits(tok.flp, tok.text);
            }
            return tok;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const size_t start = m_pos;
            while (m_pos < m_text.size()
                   && (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))
                       || m_text[m_pos] == '_')) {
                ++m_pos;
            }
            const std::string first = m_text.substr(start, m_pos - start);
            int64_t value = 0;
            if (peek(0) == '#') {
                int64_t base = 0;
                if (!decimalValue(first, base) || base < 2 || base > 16) {
                    tok.flp->v3error("Malformed VHDL based literal");
                    tok.kind = Token::END;
                    return tok;
                }
                ++m_pos;  // '#'
                const size_t dstart = m_pos;
                while (m_pos < m_text.size() && m_text[m_pos] != '#' && m_text[m_pos] != '\n') {
                    ++m_pos;
                }
                if (m_pos >= m_text.size() || m_text[m_pos] != '#') {
                    tok.flp->v3error("VHDL based literal is missing its closing '#'");
                    tok.kind = Token::END;
                    return tok;
                }
                const std::string digits = m_text.substr(dstart, m_pos - dstart);
                ++m_pos;  // '#'
                int64_t result = 0;
                for (const char d : digits) {
                    if (d == '_') continue;
                    int digit = -1;
                    if (d >= '0' && d <= '9')
                        digit = d - '0';
                    else if (d >= 'a' && d <= 'f')
                        digit = d - 'a' + 10;
                    else if (d >= 'A' && d <= 'F')
                        digit = d - 'A' + 10;
                    if (digit < 0 || digit >= base) {
                        tok.flp->v3error("Malformed digit in VHDL based literal");
                        tok.kind = Token::END;
                        return tok;
                    }
                    result = result * base + digit;
                }
                value = result;
            } else if (!decimalValue(first, value)) {
                tok.flp->v3error("Malformed VHDL integer literal");
                tok.kind = Token::END;
                return tok;
            }
            tok.kind = Token::INT;
            tok.intValue = value;
            tok.text = first;
            return tok;
        }
        if (c == '\'') {
            // Character literal ('0'); anything else is an attribute mark
            if (peek(1) != '\'' && peek(1) != '\n' && peek(2) == '\'') {
                tok.kind = Token::CHAR;
                tok.charValue = peek(1);
                m_pos += 3;
                return tok;
            }
            tok.kind = Token::SYM;
            tok.sym = '\'';
            tok.text = "'";
            ++m_pos;
            return tok;
        }
        if (c == '"') {
            ++m_pos;
            const size_t start = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '"' && m_text[m_pos] != '\n') {
                ++m_pos;
            }
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
                tok.flp->v3error("VHDL string literal is missing its closing '\"'");
                tok.kind = Token::END;
                return tok;
            }
            tok.kind = Token::STRING;
            tok.text = m_text.substr(start, m_pos - start);
            ++m_pos;
            return tok;
        }
        if (c == '(' || c == ')' || c == ';' || c == ':' || c == ',' || c == '.' || c == '='
            || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' || c == '&'
            || c == '|' || c == '#') {
            return lexSymbol(tok.flp);
        }
        tok.flp->v3error(std::string{"Unsupported: VHDL character '"} + c + "'");
        ++m_pos;
        tok.kind = Token::END;
        return tok;
    }
};

//######################################################################
// Frontend syntax tree

struct Expr;
using ExprP = std::unique_ptr<Expr>;

struct Expr final {
    enum Kind : uint8_t {
        INT_LIT,  // intValue
        BITS_LIT,  // bits
        CHAR_LIT,  // charValue
        NAME,  // text/spelling
        TRUE_LIT,
        FALSE_LIT,
        PAREN,  // a
        UNARY,  // text=operator, a
        BINARY,  // text=operator, a, b
        INDEX,  // a=prefix, args
        SLICE,  // a=prefix, b=left, c=right, text="downto"/"to"
        AGG_OTHERS,  // a=value
    } kind;
    FileLine* flp = nullptr;
    std::string text;  // Name/operator, folded
    std::string spelling;  // Name as written
    int64_t intValue = 0;
    char charValue = 0;
    std::string bits;
    ExprP a, b, c;
    std::vector<ExprP> args;

    explicit Expr(Kind kind_)
        : kind{kind_} {}
};

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

struct Stmt final {
    enum Kind : uint8_t { ASSIGN, IF } kind;
    FileLine* flp = nullptr;
    ExprP target;  // ASSIGN
    ExprP value;  // ASSIGN
    // IF: (condition, body); a null condition marks the final else
    std::vector<std::pair<ExprP, std::vector<StmtP>>> branches;

    explicit Stmt(Kind kind_)
        : kind{kind_} {}
};

struct TypeSpec final {
    std::string mark;  // Folded type mark
    FileLine* flp = nullptr;
    bool hasRange = false;
    bool ascending = false;  // "to", else "downto"
    ExprP left, right;
};

struct PortDecl final {
    FileLine* flp = nullptr;
    std::vector<std::pair<std::string, std::string>> names;  // (folded, spelling)
    VDirection dir = VDirection::INPUT;
    TypeSpec type;
};

struct Decl final {
    enum Kind : uint8_t { SIGNAL, CONSTANT } kind;
    FileLine* flp = nullptr;
    std::vector<std::pair<std::string, std::string>> names;
    TypeSpec type;
    ExprP init;
    explicit Decl(Kind kind_)
        : kind{kind_} {}
};

struct Process final {
    FileLine* flp = nullptr;
    bool hasSensitivity = false;
    std::vector<std::string> sensitivity;  // Folded names
    std::vector<StmtP> body;
};

struct Concurrent final {
    enum Kind : uint8_t { ASSIGN, PROCESS } kind;
    FileLine* flp = nullptr;
    ExprP target;  // ASSIGN
    ExprP value;  // ASSIGN
    std::unique_ptr<Process> process;  // PROCESS
    explicit Concurrent(Kind kind_)
        : kind{kind_} {}
};

struct EntityUnit final {
    std::string name;  // Folded
    std::string spelling;
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    std::vector<PortDecl> ports;
};

struct Architecture final {
    std::string name;  // Folded
    std::string entityName;  // Folded
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    std::vector<Decl> decls;
    std::vector<std::unique_ptr<Concurrent>> stmts;
};

//######################################################################
// Parser

class Parser final {
    const std::string m_filename;  // File being parsed
    Lexer m_lexer;  // Token source
    Token m_cur;  // Current token
    bool m_failed = false;  // Stop after the first error
    std::string m_libname;  // Library of the current file
    bool m_inLibrary = false;  // Current file came from a library
    std::vector<EntityUnit> m_entities;  // Parsed design units
    std::vector<Architecture> m_archs;

    void advance() { m_cur = m_lexer.next(); }
    bool isSym(const char* symText) const {
        return m_cur.kind == Token::SYM && m_cur.text == symText;
    }
    bool isId(const char* text) const { return m_cur.kind == Token::ID && m_cur.text == text; }
    bool acceptSym(const char* symText) {
        if (!isSym(symText)) return false;
        advance();
        return true;
    }
    bool acceptId(const char* text) {
        if (!isId(text)) return false;
        advance();
        return true;
    }
    void error(FileLine* flp, const std::string& message) {
        if (m_failed) return;
        flp->v3error("VHDL: " + message);
        m_failed = true;
    }
    void error(const std::string& message) { error(m_cur.flp, message); }
    void unsupported(FileLine* flp, const std::string& what) {
        if (m_failed) return;
        flp->v3error("Unsupported: VHDL " + what);
        m_failed = true;
    }
    void unsupported(const std::string& what) { unsupported(m_cur.flp, what); }
    void expectSym(const char* symText) {
        if (!acceptSym(symText))
            error(std::string{"expected '"} + symText + "', got '" + m_cur.text + "'");
    }
    bool takeId(Token& out) {
        if (m_cur.kind != Token::ID) {
            error("expected an identifier, got '" + m_cur.text + "'");
            return false;
        }
        out = m_cur;
        advance();
        return true;
    }
    bool expectId(const char* expected) {
        if (acceptId(expected)) return true;
        error(std::string{"expected '"} + expected + "', got '" + m_cur.text + "'");
        return false;
    }
    void addName(std::vector<std::pair<std::string, std::string>>& names, const Token& tok) {
        names.emplace_back(tok.text, tok.spelling);
    }

    //! Skip a context clause (`library`/`use`/`context`) up to its semicolon
    void skipContextClause() {
        int parens = 0;
        while (m_cur.kind != Token::END) {
            if (isSym("(")) ++parens;
            if (isSym(")")) --parens;
            if (parens == 0 && acceptSym(";")) return;
            advance();
        }
        unsupported("context clause that reaches the end of the file");
    }

    ExprP parseExpr();
    ExprP parseLogical();
    ExprP parseRelation();
    ExprP parseAdd();
    ExprP parseMul();
    ExprP parseUnary();
    ExprP parsePrimary();
    ExprP parseDesignator();
    bool parseRange(TypeSpec& type);
    void parseTypeSpec(TypeSpec& type);
    void parsePortClause(EntityUnit& entity);
    void parseEntity(FileLine* flp);
    void parseNameList(std::vector<std::pair<std::string, std::string>>& names);
    void parseDecl(Architecture& arch, Decl::Kind kind);
    void parseArchitecture(FileLine* flp);
    void parseConcurrentStatement(Architecture& arch);
    void parseProcess(Architecture& arch, FileLine* flp);
    std::vector<StmtP> parseStmtList(bool stopAtElse);
    StmtP parseSequentialStatement();
    StmtP parseIf(FileLine* flp);

public:
    Parser(std::string filename, std::string text, std::string libname, bool inLibrary)
        : m_filename{filename}
        , m_lexer{m_filename, std::move(text)}
        , m_libname{std::move(libname)}
        , m_inLibrary{inLibrary} {}

    void parseDesignFile();
    std::vector<EntityUnit>& entities() { return m_entities; }
    std::vector<Architecture>& archs() { return m_archs; }
};

//######################################################################
// Lowering

//! VHDL type as it survives into the committed AST
struct VType final {
    enum Kind : uint8_t { LOGIC, BIT, INT, VECTOR } kind = LOGIC;
    AstNodeDType* dtp = nullptr;
    int width = 1;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    int left = 0;
    int right = 0;
    bool ascending = false;

    bool isVector() const { return kind == VECTOR; }
    bool isInt() const { return kind == INT; }
    bool isSigned() const { return signing == VSigning::SIGNED; }
    //! Index of the least significant packed element
    int lowIndex() const { return ascending ? left : right; }
};

class Lowering final {
    AstNetlist* const m_rootp;  // Netlist to emit into
    AstModule* m_modp = nullptr;  // Module being emitted
    struct VarInfo final {
        AstVar* varp = nullptr;
        VType type;
    };
    std::map<std::string, VarInfo> m_scope;  // Signals and ports by folded name
    std::map<std::string, int64_t> m_consts;  // Integer constants by folded name
    std::vector<std::pair<std::string, std::string>> m_pendingNames;  // Duplicate detection
    struct Edge final {
        AstVar* varp = nullptr;
        VEdgeType type = VEdgeType::ET_CHANGED;
    };
    std::vector<Edge> m_edges;  // Clock edges collected while lowering a process
    bool m_initial = false;  // Emitting the time-zero evaluation

    AstNodeDType* findBasic(FileLine* flp, VBasicDTypeKwd kwd) const {
        return m_rootp->typeTablep()->findBasicDType(flp, kwd);
    }
    AstNodeDType* findVector(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing, int left,
                             int right, bool ascending) const {
        const int width = std::abs(left - right) + 1;
        return m_rootp->typeTablep()->findLogicBitDType(
            flp, kwd, VNumRange{left, right, ascending}, width, signing);
    }
    AstNodeDType* findVectorWidth(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing,
                                  int width) const {
        return findVector(flp, kwd, signing, width - 1, 0, false);
    }
    VType vectorType(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing, int width) const {
        VType type;
        type.kind = VType::VECTOR;
        type.width = width;
        type.kwd = kwd;
        type.signing = signing;
        type.left = width - 1;
        type.right = 0;
        type.ascending = false;
        type.dtp = findVectorWidth(flp, kwd, signing, width);
        return type;
    }
    VType intType(FileLine* flp) const {
        VType type;
        type.kind = VType::INT;
        type.width = 32;
        type.dtp = findBasic(flp, VBasicDTypeKwd::INT);
        return type;
    }
    VType logicType(FileLine* flp) const {
        VType type;
        type.kind = VType::LOGIC;
        type.dtp = findBasic(flp, VBasicDTypeKwd::LOGIC);
        return type;
    }
    static VType typeOfDType(AstNodeDType* dtp) {
        VType type;
        type.dtp = dtp;
        type.width = dtp->width();
        const AstBasicDType* const basicp = VN_CAST(dtp->skipRefp(), BasicDType);
        if (!basicp) {
            type.kind = VType::VECTOR;
            return type;
        }
        type.kwd = basicp->keyword();
        if (basicp->keyword() == VBasicDTypeKwd::INT) {
            type.kind = VType::INT;
        } else if (basicp->isRanged() && basicp->width() > 1) {
            type.kind = VType::VECTOR;
            type.left = basicp->left();
            type.right = basicp->right();
            type.ascending = basicp->ascending();
            type.signing = dtp->isSigned() ? VSigning::SIGNED : VSigning::UNSIGNED;
        } else {
            type.kind = basicp->keyword() == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
        }
        return type;
    }

    AstConst* makeIntConst(FileLine* flp, int64_t value) const {
        const VType type = intType(flp);
        V3Number num{flp, type.width, static_cast<uint32_t>(value)};
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(type.dtp);
        return constp;
    }
    //! Constant with one repeated logic value; bit is '0', '1', 'x' or 'z'
    AstConst* makeBitsConst(FileLine* flp, AstNodeDType* dtp, const std::string& bits) const {
        const int width = static_cast<int>(bits.size());
        V3Number num{flp, width, 0u};
        for (int i = 0; i < width; ++i) {
            const char bit = bits[width - 1 - i];  // bits[0] is the most significant bit
            if (bit == '1')
                num.setBit(i, '1');
            else if (bit == 'x')
                num.setBit(i, 'x');
            else if (bit == 'z')
                num.setBit(i, 'z');
        }
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(dtp);
        return constp;
    }
    AstConst* makeRepeatedConst(FileLine* flp, const VType& type, char bit) const {
        return makeBitsConst(flp, type.dtp, std::string(static_cast<size_t>(type.width), bit));
    }
    AstNodeExpr* makeVectorConst(FileLine* flp, const VType& type, int64_t value) const {
        V3Number num{flp, type.width, static_cast<uint32_t>(value)};
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(type.dtp);
        return constp;
    }
    AstNodeExpr* bitConst(FileLine* flp, char bit) const {
        return makeBitsConst(flp, findBasic(flp, VBasicDTypeKwd::LOGIC), std::string(1, bit));
    }
    AstNodeExpr* resizeExpr(AstNodeExpr* exprp, const VType& to, FileLine* flp) const {
        AstNodeExpr* resultp = exprp;
        const int fromWidth = exprp->width();
        if (fromWidth < to.width) {
            const bool sign = exprp->dtypep()->isSigned();
            resultp = sign ? static_cast<AstNodeExpr*>(new AstExtendS{flp, exprp, to.width})
                           : static_cast<AstNodeExpr*>(new AstExtend{flp, exprp, to.width});
        } else if (fromWidth > to.width) {
            resultp = new AstSel{flp, exprp, 0, to.width};
        }
        resultp->dtypep(to.dtp);
        return resultp;
    }

    bool resolveType(const TypeSpec& spec, VType& out);
    bool evalInt(const Expr& expr, int64_t& value) const;
    bool staticInt(const ExprP& exprp, int64_t& value) const {
        return exprp && evalInt(*exprp, value);
    }

    AstVar* lookupVar(const Expr& nameExpr) const {
        const auto it = m_scope.find(nameExpr.text);
        return it == m_scope.end() ? nullptr : it->second.varp;
    }
    bool isConversion(const std::string& name) const {
        return name == "std_logic_vector" || name == "std_ulogic_vector" || name == "bit_vector"
               || name == "unsigned" || name == "signed" || name == "to_integer"
               || name == "to_unsigned" || name == "to_signed" || name == "integer";
    }

    AstNodeExpr* lowerExpr(const Expr& expr, const VType* context);
    AstNodeExpr* lowerBinary(const Expr& expr, const VType* context);
    AstNodeExpr* lowerConversion(const Expr& expr);
    AstNodeExpr* lowerDesignator(const Expr& expr, bool write);
    AstNodeExpr* lowerSel(const Expr& expr, AstNodeExpr* basep, const VType& baseType, bool write);

    AstNodeExpr* lowerCond(const Expr& expr);
    bool containsEdgeCall(const Expr& expr) const;
    AstNode* lowerStmt(const Stmt& stmt);
    AstNode* lowerStmtList(const std::vector<StmtP>& stmts);
    AstNode* lowerIf(const Stmt& stmt);
    AstNode* lowerAssign(const Stmt& stmt, bool concurrent);
    void lowerProcess(const Process& process);

public:
    explicit Lowering(AstNetlist* rootp)
        : m_rootp{rootp} {}
    ~Lowering() = default;

    void emit(const EntityUnit& entity, const Architecture& arch);
};

//######################################################################
// Parser

ExprP Parser::parseExpr() { return parseLogical(); }

ExprP Parser::parseLogical() {
    ExprP lhsp = parseRelation();
    if (!lhsp) return nullptr;
    while (m_cur.kind == Token::ID
           && (m_cur.text == "and" || m_cur.text == "or" || m_cur.text == "xor"
               || m_cur.text == "nand" || m_cur.text == "nor" || m_cur.text == "xnor")) {
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        ExprP rhsp = parseRelation();
        if (!rhsp) return nullptr;
        node->a = std::move(lhsp);
        node->b = std::move(rhsp);
        lhsp = std::move(node);
    }
    return lhsp;
}

ExprP Parser::parseRelation() {
    ExprP lhsp = parseAdd();
    if (!lhsp) return nullptr;
    while (isSym("=") || isSym("/=") || isSym("<") || isSym("<=") || isSym(">") || isSym(">=")) {
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        ExprP rhsp = parseAdd();
        if (!rhsp) return nullptr;
        node->a = std::move(lhsp);
        node->b = std::move(rhsp);
        lhsp = std::move(node);
    }
    return lhsp;
}

ExprP Parser::parseAdd() {
    ExprP lhsp = parseMul();
    if (!lhsp) return nullptr;
    while (isSym("+") || isSym("-") || isSym("&")) {
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        ExprP rhsp = parseMul();
        if (!rhsp) return nullptr;
        node->a = std::move(lhsp);
        node->b = std::move(rhsp);
        lhsp = std::move(node);
    }
    return lhsp;
}

ExprP Parser::parseMul() {
    ExprP lhsp = parseUnary();
    if (!lhsp) return nullptr;
    while (isSym("*") || isSym("/") || isId("mod") || isId("rem")) {
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        ExprP rhsp = parseUnary();
        if (!rhsp) return nullptr;
        node->a = std::move(lhsp);
        node->b = std::move(rhsp);
        lhsp = std::move(node);
    }
    return lhsp;
}

ExprP Parser::parseUnary() {
    if (isSym("+") || isSym("-") || isId("abs") || isId("not")) {
        ExprP node = std::make_unique<Expr>(Expr::UNARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        node->a = parseUnary();
        if (!node->a) return nullptr;
        return node;
    }
    return parsePrimary();
}

ExprP Parser::parsePrimary() {
    if (m_cur.kind == Token::INT || m_cur.kind == Token::BITS || m_cur.kind == Token::CHAR) {
        ExprP node;
        if (m_cur.kind == Token::INT) {
            node = std::make_unique<Expr>(Expr::INT_LIT);
            node->intValue = m_cur.intValue;
        } else if (m_cur.kind == Token::BITS) {
            node = std::make_unique<Expr>(Expr::BITS_LIT);
            node->bits = m_cur.bits;
        } else {
            node = std::make_unique<Expr>(Expr::CHAR_LIT);
            node->charValue = m_cur.charValue;
        }
        node->flp = m_cur.flp;
        advance();
        return node;
    }
    if (m_cur.kind == Token::STRING) {
        unsupported("string literal in an expression");
        return nullptr;
    }
    if (isSym("(")) {
        advance();
        if (isId("others")) {
            advance();
            expectSym("=>");
            ExprP node = std::make_unique<Expr>(Expr::AGG_OTHERS);
            node->flp = m_cur.flp;
            node->a = parseExpr();
            if (!node->a) return nullptr;
            expectSym(")");
            return node;
        }
        ExprP inner = parseExpr();
        if (!inner) return nullptr;
        if (isSym(",")) {
            unsupported("positional aggregate");
            return nullptr;
        }
        expectSym(")");
        ExprP node = std::make_unique<Expr>(Expr::PAREN);
        node->flp = inner->flp;
        node->a = std::move(inner);
        return node;
    }
    if (isId("true") || isId("false")) {
        ExprP node = std::make_unique<Expr>(isId("true") ? Expr::TRUE_LIT : Expr::FALSE_LIT);
        node->flp = m_cur.flp;
        advance();
        return node;
    }
    if (m_cur.kind == Token::ID) return parseDesignator();
    error("expected an expression, got '" + m_cur.text + "'");
    return nullptr;
}

ExprP Parser::parseDesignator() {
    Token nameTok;
    if (!takeId(nameTok)) return nullptr;
    ExprP node = std::make_unique<Expr>(Expr::NAME);
    node->flp = nameTok.flp;
    node->text = nameTok.text;
    node->spelling = nameTok.spelling;
    for (;;) {
        if (isSym("'")) {
            unsupported("attribute");
            return nullptr;
        }
        if (isSym(".")) {
            unsupported("selected name (record member or package-qualified name)");
            return nullptr;
        }
        if (!isSym("(")) break;
        advance();
        ExprP first = parseExpr();
        if (!first) return nullptr;
        if (isId("downto") || isId("to")) {
            ExprP node2 = std::make_unique<Expr>(Expr::SLICE);
            node2->flp = node->flp;
            node2->text = m_cur.text;
            advance();
            node2->c = parseExpr();
            if (!node2->c) return nullptr;
            expectSym(")");
            node2->a = std::move(node);
            node2->b = std::move(first);
            node = std::move(node2);
        } else {
            ExprP node2 = std::make_unique<Expr>(Expr::INDEX);
            node2->flp = node->flp;
            node2->args.push_back(std::move(first));
            while (acceptSym(",")) {
                ExprP more = parseExpr();
                if (!more) return nullptr;
                node2->args.push_back(std::move(more));
            }
            expectSym(")");
            node2->a = std::move(node);
            node = std::move(node2);
        }
    }
    return node;
}

bool Parser::parseRange(TypeSpec& type) {
    ExprP left = parseExpr();
    if (!left) return false;
    if (isId("downto")) {
        type.ascending = false;
        advance();
    } else if (isId("to")) {
        type.ascending = true;
        advance();
    } else {
        error("expected 'to' or 'downto' in a range");
        return false;
    }
    ExprP right = parseExpr();
    if (!right) return false;
    type.left = std::move(left);
    type.right = std::move(right);
    type.hasRange = true;
    return true;
}

void Parser::parseTypeSpec(TypeSpec& type) {
    Token markTok;
    if (!takeId(markTok)) return;
    type.mark = markTok.text;
    type.flp = markTok.flp;
    if (isId("range")) {
        advance();
        parseRange(type);
    } else if (isSym("(")) {
        advance();
        if (!parseRange(type)) return;
        expectSym(")");
    }
}

void Parser::parseNameList(std::vector<std::pair<std::string, std::string>>& names) {
    for (;;) {
        Token nameTok;
        if (!takeId(nameTok)) return;
        addName(names, nameTok);
        if (!acceptSym(",")) break;
    }
    if (!acceptSym(":")) { error("expected ':' after a name list"); }
}

void Parser::parsePortClause(EntityUnit& entity) {
    if (!acceptSym("(")) {
        error("expected '(' after 'port'");
        return;
    }
    for (;;) {
        if (m_failed) return;
        PortDecl port;
        port.flp = m_cur.flp;
        parseNameList(port.names);
        if (m_failed) return;
        if (acceptId("in")) {
            port.dir = VDirection::INPUT;
        } else if (acceptId("out")) {
            port.dir = VDirection::OUTPUT;
        } else if (acceptId("inout")) {
            port.dir = VDirection::INOUT;
        } else if (acceptId("buffer")) {
            port.dir = VDirection::OUTPUT;  // Readable output; reads are allowed
        } else if (isId("linkage")) {
            unsupported("linkage port mode");
            return;
        } else {
            error("expected a port mode (in/out/inout/buffer), got '" + m_cur.text + "'");
            return;
        }
        parseTypeSpec(port.type);
        if (m_failed) return;
        if (isSym(":=")) {
            unsupported("port default expression");
            return;
        }
        entity.ports.push_back(std::move(port));
        if (acceptSym(";")) continue;
        if (acceptSym(")")) break;
        error("expected ';' or ')' in a port clause, got '" + m_cur.text + "'");
        return;
    }
    expectSym(";");
}

void Parser::parseEntity(FileLine* flp) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    EntityUnit entity;
    entity.flp = flp;
    entity.name = nameTok.text;
    entity.spelling = nameTok.spelling;
    entity.libname = m_libname;
    entity.inLibrary = m_inLibrary;
    if (!expectId("is")) return;
    for (;;) {
        if (m_failed) return;
        if (isId("port")) {
            advance();
            parsePortClause(entity);
            continue;
        }
        if (isId("generic")) {
            unsupported("generic clause");
            return;
        }
        if (isId("begin")) {
            unsupported("entity statement part");
            return;
        }
        if (isId("end")) break;
        error("unsupported entity declaration item '" + m_cur.text + "'");
        return;
    }
    if (!expectId("end")) return;
    (void)acceptId("entity");
    if (m_cur.kind == Token::ID) {
        Token endTok;
        if (!takeId(endTok)) return;
        if (endTok.text != entity.name) {
            error(flp, "entity end name '" + endTok.spelling + "' does not match '"
                           + entity.spelling + "'");
            return;
        }
    }
    if (!acceptSym(";")) {
        error("expected ';' after 'end' of entity");
        return;
    }
    m_entities.push_back(std::move(entity));
}

void Parser::parseDecl(Architecture& arch, Decl::Kind kind) {
    Decl decl{kind};
    decl.flp = m_cur.flp;
    parseNameList(decl.names);
    if (m_failed) return;
    parseTypeSpec(decl.type);
    if (m_failed) return;
    if (acceptSym(":=")) {
        decl.init = parseExpr();
        if (!decl.init) return;
    }
    if (isId("register") || isId("bus")) {
        unsupported("signal kind '" + m_cur.text + "'");
        return;
    }
    if (!acceptSym(";")) {
        error("expected ';' after a declaration");
        return;
    }
    arch.decls.push_back(std::move(decl));
}

void Parser::parseArchitecture(FileLine* flp) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    Architecture arch;
    arch.flp = flp;
    arch.name = nameTok.text;
    arch.libname = m_libname;
    arch.inLibrary = m_inLibrary;
    if (!expectId("of")) return;
    Token entityTok;
    if (!takeId(entityTok)) return;
    arch.entityName = entityTok.text;
    if (!expectId("is")) return;
    for (;;) {
        if (m_failed) return;
        if (isId("begin")) break;
        if (isId("use")) {
            skipContextClause();
            continue;
        }
        if (isId("signal")) {
            advance();
            parseDecl(arch, Decl::SIGNAL);
            continue;
        }
        if (isId("constant")) {
            advance();
            parseDecl(arch, Decl::CONSTANT);
            continue;
        }
        if (isId("variable") || isId("shared")) {
            unsupported("variable declaration at architecture level");
            return;
        }
        if (isId("type") || isId("subtype") || isId("alias") || isId("component")
            || isId("attribute") || isId("function") || isId("procedure") || isId("package")) {
            unsupported("architecture declarative item '" + m_cur.text + "'");
            return;
        }
        error("unexpected architecture declaration '" + m_cur.text + "'");
        return;
    }
    advance();  // 'begin'
    while (!m_failed && !isId("end") && m_cur.kind != Token::END) {
        parseConcurrentStatement(arch);
    }
    if (!expectId("end")) return;
    (void)acceptId("architecture");
    if (m_cur.kind == Token::ID) {
        Token endTok;
        if (!takeId(endTok)) return;
        if (endTok.text != arch.name) {
            error(flp, "architecture end name '" + endTok.spelling + "' does not match '"
                           + arch.name + "'");
            return;
        }
    }
    if (!acceptSym(";")) {
        error("expected ';' after 'end' of architecture");
        return;
    }
    m_archs.push_back(std::move(arch));
}

void Parser::parseConcurrentStatement(Architecture& arch) {
    if (m_failed) return;
    FileLine* const flp = m_cur.flp;
    // Optional statement label: consume `label :`
    if (m_cur.kind == Token::ID) {
        const auto saved = m_lexer.state();
        const Token ahead = m_lexer.next();
        m_lexer.restore(saved);
        if (ahead.kind == Token::SYM && ahead.text == ":") {
            advance();
            advance();
        }
    }
    if (isId("process")) {
        advance();
        parseProcess(arch, flp);
        return;
    }
    if (isId("entity") || isId("component") || isId("for") || isId("if") || isId("case")
        || isId("with") || isId("assert") || isId("block")) {
        unsupported("concurrent statement '" + m_cur.text + "'");
        return;
    }
    if (m_cur.kind != Token::ID) {
        error("expected a concurrent statement, got '" + m_cur.text + "'");
        return;
    }
    ExprP target = parseDesignator();
    if (!target) return;
    if (acceptSym("<=")) {
        auto conc = std::make_unique<Concurrent>(Concurrent::ASSIGN);
        conc->flp = flp;
        conc->target = std::move(target);
        conc->value = parseExpr();
        if (!conc->value) return;
        if (isId("when") || isId("after")) {
            unsupported("conditional or delayed concurrent assignment");
            return;
        }
        expectSym(";");
        arch.stmts.push_back(std::move(conc));
        return;
    }
    if (isSym(":=")) {
        unsupported("variable assignment at architecture level");
        return;
    }
    unsupported("component or direct entity instantiation");
}

void Parser::parseProcess(Architecture& arch, FileLine* flp) {
    Process process;
    process.flp = flp;
    if (acceptSym("(")) {
        process.hasSensitivity = true;
        if (isId("all")) {
            unsupported("process(all) sensitivity list");
            return;
        }
        for (;;) {
            Token nameTok;
            if (!takeId(nameTok)) return;
            process.sensitivity.push_back(nameTok.text);
            if (!acceptSym(",")) break;
        }
        expectSym(")");
    }
    (void)acceptId("is");
    while (!m_failed && !isId("begin")) {
        if (m_cur.kind == Token::END) {
            error("unexpected end of file in a process declarative part");
            return;
        }
        if (isId("use")) {
            skipContextClause();
            continue;
        }
        unsupported("process declarative item '" + m_cur.text + "'");
        return;
    }
    if (m_failed) return;
    advance();  // 'begin'
    process.body = parseStmtList(false);
    if (m_failed) return;
    if (!expectId("end")) return;
    (void)acceptId("process");
    if (m_cur.kind == Token::ID) advance();  // Optional end label
    if (!acceptSym(";")) {
        error("expected ';' after 'end process'");
        return;
    }
    auto conc = std::make_unique<Concurrent>(Concurrent::PROCESS);
    conc->flp = flp;
    conc->process = std::make_unique<Process>(std::move(process));
    arch.stmts.push_back(std::move(conc));
}

std::vector<StmtP> Parser::parseStmtList(bool stopAtElse) {
    std::vector<StmtP> body;
    for (;;) {
        if (m_failed) break;
        if (m_cur.kind == Token::END) break;
        if (isId("end")) break;
        if (stopAtElse && (isId("elsif") || isId("else"))) break;
        StmtP stmt = parseSequentialStatement();
        if (stmt) {
            body.push_back(std::move(stmt));
        } else if (m_failed) {
            break;
        }
    }
    return body;
}

StmtP Parser::parseSequentialStatement() {
    if (m_failed) return nullptr;
    FileLine* const flp = m_cur.flp;
    if (isId("if")) {
        advance();
        return parseIf(flp);
    }
    if (isId("case") || isId("for") || isId("while") || isId("loop") || isId("wait")
        || isId("return") || isId("report") || isId("assert") || isId("next") || isId("exit")) {
        unsupported("sequential statement '" + m_cur.text + "'");
        return nullptr;
    }
    if (isId("null")) {
        advance();
        expectSym(";");
        return nullptr;
    }
    if (m_cur.kind != Token::ID) {
        error("expected a sequential statement, got '" + m_cur.text + "'");
        return nullptr;
    }
    ExprP target = parseDesignator();
    if (!target) return nullptr;
    if (isSym(":=")) {
        unsupported("variable assignment");
        return nullptr;
    }
    if (!acceptSym("<=")) {
        error("expected '<=' in a sequential signal assignment, got '" + m_cur.text + "'");
        return nullptr;
    }
    auto stmt = std::make_unique<Stmt>(Stmt::ASSIGN);
    stmt->flp = flp;
    stmt->target = std::move(target);
    stmt->value = parseExpr();
    if (!stmt->value) return nullptr;
    if (isId("after") || isId("when")) {
        unsupported("delayed or conditional signal assignment");
        return nullptr;
    }
    expectSym(";");
    return stmt;
}

StmtP Parser::parseIf(FileLine* flp) {
    auto stmt = std::make_unique<Stmt>(Stmt::IF);
    stmt->flp = flp;
    for (;;) {
        ExprP cond = parseExpr();
        if (!cond) return nullptr;
        if (!expectId("then")) return nullptr;
        std::vector<StmtP> body = parseStmtList(true);
        if (m_failed) return nullptr;
        stmt->branches.emplace_back(std::move(cond), std::move(body));
        if (acceptId("elsif")) continue;
        if (acceptId("else")) {
            std::vector<StmtP> elseBody = parseStmtList(true);
            if (m_failed) return nullptr;
            stmt->branches.emplace_back(ExprP{}, std::move(elseBody));
        }
        break;
    }
    if (!expectId("end")) return nullptr;
    (void)acceptId("if");
    if (m_cur.kind == Token::ID) advance();  // Optional end label
    if (!acceptSym(";")) {
        error("expected ';' after 'end if'");
        return nullptr;
    }
    return stmt;
}

void Parser::parseDesignFile() {
    advance();
    while (!m_failed && m_cur.kind != Token::END) {
        if (isId("library") || isId("use") || isId("context")) {
            skipContextClause();
            continue;
        }
        if (isId("entity")) {
            FileLine* const flp = m_cur.flp;
            advance();
            parseEntity(flp);
            continue;
        }
        if (isId("architecture")) {
            FileLine* const flp = m_cur.flp;
            advance();
            parseArchitecture(flp);
            continue;
        }
        if (isId("package") || isId("configuration")) {
            unsupported("design unit '" + m_cur.text + "'");
            return;
        }
        error("unexpected token '" + m_cur.text + "' at design-unit level");
        return;
    }
}

//######################################################################
// Lowering

bool Lowering::evalInt(const Expr& expr, int64_t& value) const {
    switch (expr.kind) {
    case Expr::INT_LIT: value = expr.intValue; return true;
    case Expr::NAME: {
        const auto it = m_consts.find(expr.text);
        if (it == m_consts.end()) return false;
        value = it->second;
        return true;
    }
    case Expr::PAREN: return evalInt(*expr.a, value);
    case Expr::UNARY: {
        int64_t operand = 0;
        if (!evalInt(*expr.a, operand)) return false;
        if (expr.text == "+") {
            value = operand;
            return true;
        }
        if (expr.text == "-") {
            value = -operand;
            return true;
        }
        if (expr.text == "abs") {
            value = operand < 0 ? -operand : operand;
            return true;
        }
        return false;
    }
    case Expr::BINARY: {
        int64_t lhs = 0;
        int64_t rhs = 0;
        if (!evalInt(*expr.a, lhs) || !evalInt(*expr.b, rhs)) return false;
        const std::string& op = expr.text;
        if (op == "+") {
            value = lhs + rhs;
        } else if (op == "-") {
            value = lhs - rhs;
        } else if (op == "*") {
            value = lhs * rhs;
        } else if (op == "/") {
            if (rhs == 0) return false;
            value = lhs / rhs;
        } else if (op == "rem") {
            if (rhs == 0) return false;
            value = lhs - (lhs / rhs) * rhs;
        } else if (op == "mod") {
            if (rhs == 0) return false;
            value = lhs - (lhs / rhs) * rhs;
            if (value != 0 && ((value < 0) != (rhs < 0))) value += rhs;
        } else {
            return false;
        }
        return true;
    }
    default: return false;
    }
}

bool Lowering::resolveType(const TypeSpec& spec, VType& out) {
    FileLine* const flp = spec.flp;
    if (!flp) return false;
    const std::string& mark = spec.mark;
    if (mark == "std_logic" || mark == "std_ulogic" || mark == "bit" || mark == "boolean") {
        if (spec.hasRange) {
            flp->v3error("VHDL: range constraint on scalar type '" + mark + "'");
            return false;
        }
        out.kind = mark == "bit" ? VType::BIT : VType::LOGIC;
        out.dtp = findBasic(flp, mark == "bit" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC);
        return true;
    }
    if (mark == "integer" || mark == "natural" || mark == "positive") {
        out = intType(flp);
        if (spec.hasRange) {
            int64_t left = 0;
            int64_t right = 0;
            if (!staticInt(spec.left, left) || !staticInt(spec.right, right)) {
                flp->v3error("Unsupported: VHDL non-static integer subtype range");
                return false;
            }
        }
        return true;
    }
    const bool vectorMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                            || mark == "bit_vector" || mark == "unsigned" || mark == "signed";
    if (!vectorMark) {
        flp->v3error("Unsupported: VHDL type '" + mark + "'");
        return false;
    }
    if (!spec.hasRange) {
        flp->v3error("Unsupported: VHDL unconstrained type '" + mark + "'");
        return false;
    }
    int64_t left = 0;
    int64_t right = 0;
    if (!staticInt(spec.left, left) || !staticInt(spec.right, right)) {
        flp->v3error("Unsupported: VHDL non-static array range");
        return false;
    }
    const VBasicDTypeKwd kwd = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
    const VSigning signing = mark == "signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
    out = vectorType(flp, kwd, signing,
                     std::abs(static_cast<int>(left) - static_cast<int>(right)) + 1);
    out.left = static_cast<int>(left);
    out.right = static_cast<int>(right);
    out.ascending = spec.ascending;
    out.dtp = findVector(flp, kwd, signing, out.left, out.right, out.ascending);
    return true;
}

bool Lowering::containsEdgeCall(const Expr& expr) const {
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        return true;
    }
    if (expr.a && containsEdgeCall(*expr.a)) return true;
    if (expr.b && containsEdgeCall(*expr.b)) return true;
    if (expr.c && containsEdgeCall(*expr.c)) return true;
    for (const ExprP& argp : expr.args) {
        if (argp && containsEdgeCall(*argp)) return true;
    }
    return false;
}

AstNodeExpr* Lowering::lowerExpr(const Expr& expr, const VType* context) {
    FileLine* const flp = expr.flp;
    switch (expr.kind) {
    case Expr::INT_LIT: return makeIntConst(flp, expr.intValue);
    case Expr::TRUE_LIT: return bitConst(flp, '1');
    case Expr::FALSE_LIT: return bitConst(flp, '0');
    case Expr::CHAR_LIT: {
        const char bit = vhdlLogicBit(expr.charValue);
        if (bit == 0) {
            flp->v3error(std::string{"Unsupported: VHDL character literal '"} + expr.charValue
                         + "' outside a logic context");
            return nullptr;
        }
        if (bit == 'z') {
            flp->v3error("Unsupported: VHDL 'Z' literal");
            return nullptr;
        }
        return bitConst(flp, bit);
    }
    case Expr::BITS_LIT: {
        const int width = static_cast<int>(expr.bits.size());
        AstNodeDType* dtp = nullptr;
        if (context && context->isVector() && context->width == width) {
            dtp = context->dtp;
        } else {
            dtp = findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, width);
        }
        return makeBitsConst(flp, dtp, expr.bits);
    }
    case Expr::NAME: {
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) return makeIntConst(flp, constIt->second);
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            flp->v3error("VHDL: unknown identifier '" + expr.spelling + "'");
            return nullptr;
        }
        return new AstVarRef{flp, it->second.varp, VAccess::READ};
    }
    case Expr::PAREN: return lowerExpr(*expr.a, context);
    case Expr::UNARY: {
        AstNodeExpr* operandp = lowerExpr(*expr.a, nullptr);
        if (!operandp) return nullptr;
        const VType type = typeOfDType(operandp->dtypep());
        if (expr.text == "not") {
            AstNot* const notp = new AstNot{flp, operandp};
            notp->dtypep(type.dtp);
            return notp;
        }
        if (expr.text == "+") return operandp;
        if (expr.text == "-") {
            if (!type.isInt()) {
                flp->v3error("Unsupported: VHDL unary '-' on a non-integer value");
                operandp->deleteTree();
                return nullptr;
            }
            AstNegate* const negp = new AstNegate{flp, operandp};
            negp->dtypep(type.dtp);
            return negp;
        }
        flp->v3error("Unsupported: VHDL unary operator '" + expr.text + "'");
        operandp->deleteTree();
        return nullptr;
    }
    case Expr::BINARY: return lowerBinary(expr, context);
    case Expr::INDEX:
        if (expr.a && expr.a->kind == Expr::NAME && isConversion(expr.a->text)) {
            return lowerConversion(expr);
        }
        return lowerDesignator(expr, false);
    case Expr::SLICE: return lowerDesignator(expr, false);
    case Expr::AGG_OTHERS: {
        if (!context) {
            flp->v3error("Unsupported: VHDL aggregate without a known target type");
            return nullptr;
        }
        char bit = 0;
        if (expr.a->kind == Expr::CHAR_LIT) {
            bit = vhdlLogicBit(expr.a->charValue);
        } else if (expr.a->kind == Expr::BITS_LIT && expr.a->bits.size() == 1) {
            bit = expr.a->bits[0];
        }
        if (bit == 0) {
            flp->v3error("Unsupported: VHDL aggregate value");
            return nullptr;
        }
        if (bit == 'z') {
            flp->v3error("Unsupported: VHDL 'Z' literal");
            return nullptr;
        }
        return makeRepeatedConst(flp, *context, bit);
    }
    }
    return nullptr;
}

AstNodeExpr* Lowering::lowerBinary(const Expr& expr, const VType* context) {
    FileLine* const flp = expr.flp;
    const std::string& op = expr.text;
    AstNodeExpr* lp = lowerExpr(*expr.a, nullptr);
    if (!lp) return nullptr;
    AstNodeExpr* rp = lowerExpr(*expr.b, nullptr);
    if (!rp) {
        lp->deleteTree();
        return nullptr;
    }
    VType lt = typeOfDType(lp->dtypep());
    VType rt = typeOfDType(rp->dtypep());
    AstNodeDType* const bitDtp = findBasic(flp, VBasicDTypeKwd::LOGIC);

    const bool relational
        = op == "=" || op == "/=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    if (relational) {
        if (lt.isInt() && rt.isInt()) {
            if (op == "=") return new AstEq{flp, lp, rp};
            if (op == "/=") return new AstNeq{flp, lp, rp};
            if (op == "<") return new AstLtS{flp, lp, rp};
            if (op == "<=") return new AstLteS{flp, lp, rp};
            if (op == ">") return new AstGtS{flp, lp, rp};
            return new AstGteS{flp, lp, rp};
        }
        if (lt.isInt() != rt.isInt()) {
            flp->v3error("Unsupported: VHDL comparison between an integer and a vector");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (lt.isVector() && lt.isSigned() != rt.isSigned()) {
            flp->v3error("Unsupported: VHDL comparison between signed and unsigned operands");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (lt.isVector()) {
            const int width = std::max(lt.width, rt.width);
            const VType widenType = vectorType(flp, lt.kwd, lt.signing, width);
            lp = resizeExpr(lp, widenType, flp);
            rp = resizeExpr(rp, widenType, flp);
        }
        AstNodeExpr* cmpp = nullptr;
        const bool sign = lt.isSigned();
        if (op == "=") {
            cmpp = new AstEq{flp, lp, rp};
        } else if (op == "/=") {
            cmpp = new AstNeq{flp, lp, rp};
        } else if (op == "<") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstLtS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstLt{flp, lp, rp});
        } else if (op == "<=") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstLteS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstLte{flp, lp, rp});
        } else if (op == ">") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstGtS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstGt{flp, lp, rp});
        } else {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstGteS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstGte{flp, lp, rp});
        }
        cmpp->dtypep(bitDtp);
        return cmpp;
    }

    if (op == "&") {
        if (!lt.isVector() && !rt.isVector()) {
            if (lt.kind != rt.kind) {
                flp->v3error("Unsupported: VHDL concatenation of mismatched element types");
                lp->deleteTree();
                rp->deleteTree();
                return nullptr;
            }
        } else if (lt.isVector() && rt.isVector() && lt.kwd != rt.kwd) {
            flp->v3error("Unsupported: VHDL concatenation of mismatched element types");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (lt.isInt() || rt.isInt()) {
            flp->v3error("Unsupported: VHDL concatenation with an integer operand");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        const VBasicDTypeKwd kwd = lt.isVector() ? lt.kwd : rt.kwd;
        const VType resultType = vectorType(flp, kwd, VSigning::UNSIGNED, lt.width + rt.width);
        AstConcat* const concatp = new AstConcat{flp, lp, rp};
        concatp->dtypep(resultType.dtp);
        return concatp;
    }

    const bool logical
        = op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor" || op == "xnor";
    if (logical) {
        if (lt.width != rt.width || lt.kind != rt.kind) {
            flp->v3error("Unsupported: VHDL logical operation on operands of different types");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        AstNodeExpr* resultp = nullptr;
        if (op == "and" || op == "nand") {
            resultp = new AstAnd{flp, lp, rp};
        } else if (op == "or" || op == "nor") {
            resultp = new AstOr{flp, lp, rp};
        } else {
            resultp = new AstXor{flp, lp, rp};
        }
        resultp->dtypep(lt.dtp);
        if (op == "nand" || op == "nor" || op == "xnor") {
            AstNot* const notp = new AstNot{flp, resultp};
            notp->dtypep(lt.dtp);
            return notp;
        }
        return resultp;
    }

    const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/";
    if (!arithmetic) {
        flp->v3error("Unsupported: VHDL operator '" + op + "'");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    if (lt.isInt() && rt.isInt()) {
        AstNodeExpr* resultp = nullptr;
        if (op == "+") {
            resultp = new AstAdd{flp, lp, rp};
        } else if (op == "-") {
            resultp = new AstSub{flp, lp, rp};
        } else if (op == "*") {
            resultp = new AstMul{flp, lp, rp};
        } else {
            resultp = new AstDivS{flp, lp, rp};
        }
        resultp->dtypep(lt.dtp);
        return resultp;
    }
    // A static integer operand acts as a natural/signed literal of the vector operand's type
    if (lt.isInt() != rt.isInt()) {
        const Expr& intExpr = lt.isInt() ? *expr.a : *expr.b;
        int64_t value = 0;
        if (!evalInt(intExpr, value)) {
            flp->v3error("Unsupported: VHDL arithmetic between an integer expression and a"
                         " vector");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        const VType& vectorT = lt.isInt() ? rt : lt;
        AstNodeExpr* const constp = makeVectorConst(flp, vectorT, value);
        if (lt.isInt()) {
            lp->deleteTree();
            lp = constp;
            lt = vectorT;
        } else {
            rp->deleteTree();
            rp = constp;
            rt = vectorT;
        }
    }
    if (!lt.isVector() || !rt.isVector() || lt.kwd != rt.kwd) {
        flp->v3error("Unsupported: VHDL arithmetic on non-numeric operands");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    if (lt.isSigned() != rt.isSigned()) {
        flp->v3error("Unsupported: VHDL arithmetic between signed and unsigned operands");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    const VSigning signing = lt.signing;
    int resultWidth = 0;
    if (op == "*") {
        resultWidth = lt.width + rt.width;
    } else if (op == "/") {
        resultWidth = lt.width;
    } else {
        resultWidth = std::max(lt.width, rt.width);
    }
    const int opWidth = std::max(resultWidth, std::max(lt.width, rt.width));
    const VType opType = vectorType(flp, lt.kwd, signing, opWidth);
    lp = resizeExpr(lp, opType, flp);
    rp = resizeExpr(rp, opType, flp);
    AstNodeExpr* resultp = nullptr;
    if (op == "+") {
        resultp = new AstAdd{flp, lp, rp};
    } else if (op == "-") {
        resultp = new AstSub{flp, lp, rp};
    } else if (op == "*") {
        resultp = new AstMul{flp, lp, rp};
    } else {
        resultp = signing == VSigning::SIGNED ? static_cast<AstNodeExpr*>(new AstDivS{flp, lp, rp})
                                              : static_cast<AstNodeExpr*>(new AstDiv{flp, lp, rp});
    }
    const VType resultType = vectorType(flp, lt.kwd, signing, resultWidth);
    if (resultWidth != opWidth) {
        AstSel* const selp = new AstSel{flp, resultp, 0, resultWidth};
        selp->dtypep(resultType.dtp);
        return selp;
    }
    resultp->dtypep(resultType.dtp);
    return resultp;
}

AstNodeExpr* Lowering::lowerConversion(const Expr& expr) {
    FileLine* const flp = expr.flp;
    const std::string& name = expr.a->text;
    if (expr.args.size() == 2) {
        if (name == "to_unsigned" || name == "to_signed") {
            AstNodeExpr* valuep = lowerExpr(*expr.args[0], nullptr);
            if (!valuep) return nullptr;
            const VType valueType = typeOfDType(valuep->dtypep());
            int64_t width = 0;
            if (!staticInt(expr.args[1], width) || width <= 0 || width > 4096) {
                flp->v3error("Unsupported: VHDL non-static or invalid conversion width");
                valuep->deleteTree();
                return nullptr;
            }
            if (!valueType.isInt()) {
                flp->v3error("Unsupported: VHDL " + name + " of a non-integer value");
                valuep->deleteTree();
                return nullptr;
            }
            const VType target
                = vectorType(flp, VBasicDTypeKwd::LOGIC,
                             name == "to_signed" ? VSigning::SIGNED : VSigning::UNSIGNED,
                             static_cast<int>(width));
            return resizeExpr(valuep, target, flp);
        }
        flp->v3error("Unsupported: VHDL conversion '" + name + "' with two arguments");
        return nullptr;
    }
    if (expr.args.size() != 1) {
        flp->v3error("Unsupported: VHDL conversion '" + name + "' with "
                     + std::to_string(expr.args.size()) + " arguments");
        return nullptr;
    }
    AstNodeExpr* valuep = lowerExpr(*expr.args[0], nullptr);
    if (!valuep) return nullptr;
    const VType valueType = typeOfDType(valuep->dtypep());
    if (name == "to_integer" || name == "integer") {
        if (valueType.isInt()) return valuep;
        if (!valueType.isVector() || valueType.width > 32) {
            flp->v3error("Unsupported: VHDL " + name + " of this operand");
            valuep->deleteTree();
            return nullptr;
        }
        return resizeExpr(valuep, intType(flp), flp);
    }
    if (!valueType.isVector()) {
        flp->v3error("Unsupported: VHDL conversion of a non-vector value");
        valuep->deleteTree();
        return nullptr;
    }
    VBasicDTypeKwd kwd = valueType.kwd;
    VSigning signing = VSigning::UNSIGNED;
    if (name == "signed") {
        signing = VSigning::SIGNED;
    } else if (name == "bit_vector") {
        kwd = VBasicDTypeKwd::BIT;
    }
    const VType target = vectorType(flp, kwd, signing, valueType.width);
    valuep->dtypep(target.dtp);
    return valuep;
}

AstNodeExpr* Lowering::lowerDesignator(const Expr& expr, bool write) {
    FileLine* const flp = expr.flp;
    if (expr.kind == Expr::NAME) {
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) {
            if (write) {
                flp->v3error("VHDL: cannot assign to constant '" + expr.spelling + "'");
                return nullptr;
            }
            return makeIntConst(flp, constIt->second);
        }
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            flp->v3error("VHDL: unknown identifier '" + expr.spelling + "'");
            return nullptr;
        }
        AstVar* const varp = it->second.varp;
        if (write && varp->isInput()) {
            flp->v3error("VHDL: cannot assign to input port '" + expr.spelling + "'");
            return nullptr;
        }
        return new AstVarRef{flp, varp, write ? VAccess::WRITE : VAccess::READ};
    }
    if (expr.kind == Expr::INDEX || expr.kind == Expr::SLICE) {
        if (!expr.a
            || (expr.a->kind != Expr::NAME && expr.a->kind != Expr::INDEX
                && expr.a->kind != Expr::SLICE)) {
            flp->v3error("Unsupported: VHDL selection of a non-name expression");
            return nullptr;
        }
        AstNodeExpr* const basep = lowerDesignator(*expr.a, write);
        if (!basep) return nullptr;
        return lowerSel(expr, basep, typeOfDType(basep->dtypep()), write);
    }
    flp->v3error("Unsupported: VHDL expression as an assignment target");
    return nullptr;
}

AstNodeExpr* Lowering::lowerSel(const Expr& expr, AstNodeExpr* basep, const VType& baseType,
                                bool write) {
    (void)write;
    FileLine* const flp = expr.flp;
    if (!baseType.isVector()) {
        flp->v3error("Unsupported: VHDL index or slice of a non-array value");
        basep->deleteTree();
        return nullptr;
    }
    VType elemType;
    elemType.kind = baseType.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
    elemType.dtp = findBasic(flp, baseType.kwd);
    const int lowIndex = baseType.lowIndex();
    if (expr.kind == Expr::INDEX) {
        if (expr.args.size() != 1) {
            flp->v3error("Unsupported: VHDL index with multiple arguments");
            basep->deleteTree();
            return nullptr;
        }
        AstNodeExpr* indexp = lowerExpr(*expr.args[0], nullptr);
        if (!indexp) {
            basep->deleteTree();
            return nullptr;
        }
        if (!typeOfDType(indexp->dtypep()).isInt()) {
            flp->v3error("Unsupported: VHDL non-integer index");
            indexp->deleteTree();
            basep->deleteTree();
            return nullptr;
        }
        AstNodeExpr* offsetp = indexp;
        if (lowIndex != 0) { offsetp = new AstSub{flp, indexp, makeIntConst(flp, lowIndex)}; }
        offsetp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
        AstSel* const selp = new AstSel{flp, basep, offsetp, 1};
        selp->dtypep(elemType.dtp);
        return selp;
    }
    int64_t leftValue = 0;
    int64_t rightValue = 0;
    if (!staticInt(expr.b, leftValue) || !staticInt(expr.c, rightValue)) {
        flp->v3error("Unsupported: VHDL non-static slice bounds");
        basep->deleteTree();
        return nullptr;
    }
    int hi = 0;
    int lo = 0;
    if (expr.text == "downto") {
        hi = static_cast<int>(leftValue);
        lo = static_cast<int>(rightValue);
    } else {
        lo = static_cast<int>(leftValue);
        hi = static_cast<int>(rightValue);
    }
    if (hi < lo) {
        flp->v3error("VHDL: empty or reversed slice");
        basep->deleteTree();
        return nullptr;
    }
    const int width = hi - lo + 1;
    const int offset = lo - lowIndex;
    if (offset < 0 || offset + width > baseType.width) {
        flp->v3error("VHDL: slice is out of range of the array");
        basep->deleteTree();
        return nullptr;
    }
    AstSel* const selp = new AstSel{flp, basep, offset, width};
    const VType sliceType = vectorType(flp, baseType.kwd, baseType.signing, width);
    selp->dtypep(sliceType.dtp);
    return selp;
}

AstNodeExpr* Lowering::lowerCond(const Expr& expr) {
    FileLine* const flp = expr.flp;
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        if (expr.args.size() != 1 || expr.args[0]->kind != Expr::NAME) {
            flp->v3error("Unsupported: VHDL edge function argument");
            return nullptr;
        }
        AstVar* const varp = lookupVar(*expr.args[0]);
        if (!varp) {
            flp->v3error("VHDL: unknown signal '" + expr.args[0]->spelling
                         + "' in an edge function");
            return nullptr;
        }
        Edge edge;
        edge.varp = varp;
        edge.type = expr.a->text == "rising_edge" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
        m_edges.push_back(edge);
        // The sensitivity item supplies the edge; at time zero no signal has had an event
        return bitConst(flp, m_initial ? '0' : '1');
    }
    if (containsEdgeCall(expr)) {
        flp->v3error("Unsupported: VHDL clock edge in a compound condition");
        return nullptr;
    }
    AstNodeExpr* condp = lowerExpr(expr, nullptr);
    if (!condp) return nullptr;
    if (condp->width() != 1) {
        flp->v3error("VHDL: condition is not a 1-bit value");
        condp->deleteTree();
        return nullptr;
    }
    return condp;
}

AstNode* Lowering::lowerAssign(const Stmt& stmt, bool concurrent) {
    AstNodeExpr* lhsp = lowerDesignator(*stmt.target, true);
    if (!lhsp) return nullptr;
    if (!concurrent && !VN_IS(lhsp, VarRef)) {
        stmt.flp->v3error("Unsupported: VHDL assignment to a slice or element from a process");
        lhsp->deleteTree();
        return nullptr;
    }
    const VType ltype = typeOfDType(lhsp->dtypep());
    AstNodeExpr* rhsp = lowerExpr(*stmt.value, &ltype);
    if (!rhsp) {
        lhsp->deleteTree();
        return nullptr;
    }
    rhsp = resizeExpr(rhsp, ltype, stmt.flp);
    if (concurrent) return new AstAssignW{stmt.flp, lhsp, rhsp};
    if (m_initial) return new AstAssign{stmt.flp, lhsp, rhsp};
    return new AstAssignDly{stmt.flp, lhsp, rhsp};
}

AstNode* Lowering::lowerIf(const Stmt& stmt) {
    AstNode* elsesp = nullptr;
    for (std::size_t i = stmt.branches.size(); i-- > 0;) {
        const auto& branch = stmt.branches[i];
        AstNode* bodyp = nullptr;
        for (const StmtP& stmtp : branch.second) {
            AstNode* const nodep = lowerStmt(*stmtp);
            if (nodep) bodyp = bodyp ? AstNode::addNext<AstNode, AstNode>(bodyp, nodep) : nodep;
        }
        if (!branch.first) {
            elsesp = bodyp;
            continue;
        }
        AstNodeExpr* const condp = lowerCond(*branch.first);
        if (!condp) {
            if (bodyp) bodyp->deleteTree();
            continue;
        }
        const AstConst* const constp = VN_CAST(condp, Const);
        if (constp && constp->num().width() == 1 && !constp->num().isFourState()) {
            const bool taken = constp->num().toUInt() != 0;
            condp->deleteTree();
            if (taken) {
                if (elsesp) elsesp->deleteTree();
                elsesp = bodyp;
            } else if (bodyp) {
                bodyp->deleteTree();
            }
            continue;
        }
        elsesp = new AstIf{stmt.flp, condp, bodyp, elsesp};
    }
    return elsesp;
}

AstNode* Lowering::lowerStmt(const Stmt& stmt) {
    if (stmt.kind == Stmt::ASSIGN) return lowerAssign(stmt, false);
    if (stmt.kind == Stmt::IF) return lowerIf(stmt);
    return nullptr;
}

AstNode* Lowering::lowerStmtList(const std::vector<StmtP>& stmts) {
    AstNode* headp = nullptr;
    for (const StmtP& stmtp : stmts) {
        AstNode* const nodep = lowerStmt(*stmtp);
        if (nodep) headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
    }
    return headp;
}

void Lowering::lowerProcess(const Process& process) {
    if (!process.hasSensitivity) {
        process.flp->v3error(
            "Unsupported: VHDL process without a sensitivity list (wait-based processes)");
        return;
    }
    std::vector<AstVar*> sensVars;
    for (const std::string& name : process.sensitivity) {
        const auto it = m_scope.find(name);
        if (it == m_scope.end()) {
            process.flp->v3error("VHDL: unknown signal '" + name + "' in a sensitivity list");
            return;
        }
        sensVars.push_back(it->second.varp);
    }

    m_edges.clear();
    m_initial = false;
    AstNode* const bodyp = lowerStmtList(process.body);
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    m_initial = true;
    AstNode* const initp = lowerStmtList(process.body);
    m_initial = false;

    bool bad = false;
    for (const Edge& edge : edges) {
        if (std::find(sensVars.begin(), sensVars.end(), edge.varp) == sensVars.end()) {
            process.flp->v3error(
                "Unsupported: VHDL clock edge on a signal outside the sensitivity list");
            bad = true;
        }
    }
    if (bad) {
        if (bodyp) bodyp->deleteTree();
        if (initp) initp->deleteTree();
        return;
    }

    AstSenItem* itemsp = nullptr;
    for (AstVar* const varp : sensVars) {
        VEdgeType type = VEdgeType::ET_CHANGED;
        for (const Edge& edge : edges) {
            if (edge.varp == varp) type = edge.type;
        }
        AstSenItem* const itemp
            = new AstSenItem{process.flp, type, new AstVarRef{process.flp, varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    if (bodyp) {
        m_modp->addStmtsp(new AstAlways{process.flp, VAlwaysKwd::ALWAYS,
                                        new AstSenTree{process.flp, itemsp}, bodyp});
    } else if (itemsp) {
        itemsp->deleteTree();
    }
    if (!edges.empty() && initp) {
        m_modp->addStmtsp(new AstInitialStatic{process.flp, initp});
    } else if (initp) {
        initp->deleteTree();
    }
}

void Lowering::emit(const EntityUnit& entity, const Architecture& arch) {
    m_modp = new AstModule{entity.flp, entity.spelling, entity.libname};
    m_modp->inLibrary(entity.inLibrary || arch.inLibrary);

    int pinNum = 0;
    for (const PortDecl& port : entity.ports) {
        VType type;
        if (!resolveType(port.type, type)) continue;
        for (const auto& name : port.names) {
            if (!m_scope.emplace(name.first, VarInfo{}).second) {
                port.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                continue;
            }
            AstVar* const varp = new AstVar{port.flp, VVarType::PORT, name.second, type.dtp};
            varp->direction(port.dir);
            varp->pinNum(++pinNum);
            // VHDL ports are signals, so procedural assignment is legal
            varp->declTyped(true);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
        }
    }

    for (const Decl& decl : arch.decls) {
        if (decl.kind == Decl::CONSTANT) {
            if (!decl.init) {
                decl.flp->v3error("VHDL: constant without a value");
                continue;
            }
            int64_t value = 0;
            if (!evalInt(*decl.init, value)) {
                decl.flp->v3error(
                    "Unsupported: VHDL constant value that is not a static integer expression");
                continue;
            }
            for (const auto& name : decl.names) {
                if (m_scope.count(name.first) || m_consts.count(name.first)) {
                    decl.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                    continue;
                }
                m_consts[name.first] = value;
            }
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            if (m_scope.count(name.first) || m_consts.count(name.first)) {
                decl.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                continue;
            }
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, name.second, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.init) {
                AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
                AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
                if (!rhsp) {
                    lhsp->deleteTree();
                    continue;
                }
                rhsp = resizeExpr(rhsp, type, decl.flp);
                m_modp->addStmtsp(
                    new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
            }
        }
    }

    for (const auto& stmtp : arch.stmts) {
        if (stmtp->kind == Concurrent::ASSIGN) {
            AstNodeExpr* const lhsp = lowerDesignator(*stmtp->target, true);
            if (!lhsp) continue;
            const VType ltype = typeOfDType(lhsp->dtypep());
            AstNodeExpr* rhsp = lowerExpr(*stmtp->value, &ltype);
            if (!rhsp) {
                lhsp->deleteTree();
                continue;
            }
            rhsp = resizeExpr(rhsp, ltype, stmtp->flp);
            m_modp->addStmtsp(new AstAlways{stmtp->flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                                            new AstAssignW{stmtp->flp, lhsp, rhsp}});
        } else {
            lowerProcess(*stmtp->process);
        }
    }

    m_rootp->addModulesp(m_modp);
    m_modp = nullptr;
    m_scope.clear();
    m_consts.clear();
}

}  // namespace

//######################################################################
// V3VhdlNative

void V3VhdlNative::emit(AstNetlist* rootp, const std::vector<V3VhdlNativeFile>& files) {
    std::vector<EntityUnit> entities;
    std::vector<Architecture> archs;
    for (const V3VhdlNativeFile& file : files) {
        const std::unique_ptr<std::ifstream> ifsp{V3File::new_ifstream(file.filename)};
        if (!*ifsp) {
            v3Global.rootp()->fileline()->v3error("Cannot read VHDL file '" + file.filename + "'");
            continue;
        }
        const std::string text{std::istreambuf_iterator<char>{*ifsp},
                               std::istreambuf_iterator<char>{}};
        Parser parser{file.filename, text, file.libname, file.inLibrary};
        parser.parseDesignFile();
        for (EntityUnit& entity : parser.entities()) entities.push_back(std::move(entity));
        for (Architecture& arch : parser.archs()) archs.push_back(std::move(arch));
    }

    // Bind each entity to one architecture; the last analyzed one is the default
    std::map<std::string, const Architecture*> archByEntity;
    for (const Architecture& arch : archs) archByEntity[arch.entityName] = &arch;

    std::set<std::string> entityNames;
    for (const EntityUnit& entity : entities) {
        if (!entityNames.insert(entity.name).second) {
            entity.flp->v3error("Duplicate VHDL entity: '" + entity.spelling + "'");
            continue;
        }
        const auto it = archByEntity.find(entity.name);
        if (it == archByEntity.end()) {
            entity.flp->v3error("Entity '" + entity.spelling
                                + "' has no architecture; cannot elaborate it");
            continue;
        }
        Lowering lowering{rootp};
        lowering.emit(entity, *it->second);
    }
    for (const Architecture& arch : archs) {
        if (!entityNames.count(arch.entityName)) {
            arch.flp->v3error("VHDL architecture does not name a declared entity");
        }
    }
}
