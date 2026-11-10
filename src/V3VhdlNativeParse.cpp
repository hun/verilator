// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: parser
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
// Recursive-descent VHDL parser. Produces the syntax tree in
// V3VhdlNativeAst.h without any name resolution: every `Name(args)` suffix
// becomes one generic index/slice node whose meaning (index, slice, call,
// conversion or attribute) is decided during semantic analysis.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3Error.h"
#include "V3File.h"
#include "V3Global.h"
#include "V3VhdlNativeAst.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

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
        unsupportedError(flp, what);
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
    bool parseChoices(std::vector<ExprP>& choices);
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
    while (isSym("=") || isSym("/=") || isSym("<") || isSym("<=") || isSym(">") || isSym(">=")
           || isSym("?=") || isSym("?/=") || isSym("?<") || isSym("?<=") || isSym("?>")
           || isSym("?>=")) {
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
        // A string of logic characters is a bit-vector value, e.g. "1001" or "10--"
        std::string bits;
        bool allBits = true;
        for (const char c : m_cur.text) {
            const char bit = c == '-' ? '-' : vhdlLogicBit(c);
            if (bit == 0) {
                allBits = false;
                break;
            }
            bits.push_back(bit);
        }
        ExprP node = std::make_unique<Expr>(allBits ? Expr::BITS_LIT : Expr::STRING_LIT);
        node->flp = m_cur.flp;
        if (allBits) {
            node->bits = std::move(bits);
        } else {
            node->spelling = m_cur.text;
        }
        advance();
        return node;
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
            port.dir = PortMode::IN;
        } else if (acceptId("out")) {
            port.dir = PortMode::OUT;
        } else if (acceptId("inout")) {
            port.dir = PortMode::INOUT;
        } else if (acceptId("buffer")) {
            port.dir = PortMode::OUT;  // Readable output; reads are allowed
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

//! Parse the choice list of one selected-assignment alternative; empty means "others"
bool Parser::parseChoices(std::vector<ExprP>& choices) {
    if (isId("others")) {
        advance();
        return true;
    }
    for (;;) {
        ExprP choice = parseExpr();
        if (!choice) return false;
        if (isId("to") || isId("downto")) {
            ExprP range = std::make_unique<Expr>(Expr::SLICE);
            range->flp = choice->flp;
            range->text = m_cur.text;
            advance();
            range->b = std::move(choice);
            range->c = parseExpr();
            if (!range->c) return false;
            choice = std::move(range);
        }
        choices.push_back(std::move(choice));
        if (!acceptSym("|")) return true;
    }
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
    if (isId("with")) {
        advance();
        auto conc = std::make_unique<Concurrent>(Concurrent::SEL_ASSIGN);
        conc->flp = flp;
        conc->selector = parseExpr();
        if (!conc->selector) return;
        if (!expectId("select")) return;
        conc->target = parseDesignator();
        if (!conc->target) return;
        if (!acceptSym("<=")) {
            error("expected '<=' in a selected signal assignment");
            return;
        }
        for (;;) {
            ExprP value = parseExpr();
            if (!value) return;
            if (!expectId("when")) return;
            std::vector<ExprP> choices;
            if (!parseChoices(choices)) return;
            conc->choices.emplace_back(std::move(choices), std::move(value));
            if (!acceptSym(",")) break;
        }
        expectSym(";");
        arch.stmts.push_back(std::move(conc));
        return;
    }
    if (isId("entity") || isId("component") || isId("for") || isId("if") || isId("case")
        || isId("assert") || isId("block")) {
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
        if (isId("transport") || isId("inertial")) {
            unsupported("delayed signal assignment ('" + m_cur.text + "')");
            return;
        }
        conc->value = parseExpr();
        if (!conc->value) return;
        if (isId("when")) {
            conc->kind = Concurrent::COND_ASSIGN;
            conc->alternatives.emplace_back(std::move(conc->value), nullptr);
            while (acceptId("when")) {
                ExprP cond = parseExpr();
                if (!cond) return;
                conc->alternatives.back().second = std::move(cond);
                if (!acceptId("else")) break;
                ExprP value = parseExpr();
                if (!value) return;
                conc->alternatives.emplace_back(std::move(value), nullptr);
            }
        }
        if (isId("after")) {
            unsupported("delayed signal assignment ('after')");
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
        unsupported("conditional or delayed signal assignment in a process");
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
// File entry point

void parseFile(const V3VhdlNativeFile& file, DesignLibrary& library) {
    const std::unique_ptr<std::ifstream> ifsp{V3File::new_ifstream(file.filename)};
    if (!*ifsp) {
        v3Global.rootp()->fileline()->v3error("Cannot read VHDL file '" + file.filename + "'");
        return;
    }
    const std::string text{std::istreambuf_iterator<char>{*ifsp},
                           std::istreambuf_iterator<char>{}};
    Parser parser{file.filename, text, file.libname, file.inLibrary};
    parser.parseDesignFile();
    for (EntityUnit& entity : parser.entities()) library.entities.push_back(std::move(entity));
    for (Architecture& arch : parser.archs()) library.archs.push_back(std::move(arch));
}

}  // namespace vhdlnative
