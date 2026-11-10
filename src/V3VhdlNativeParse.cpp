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
    std::vector<PackageUnit> m_packages;  // Parsed package specs and bodies
    // Packages named by the context clauses seen so far
    std::vector<std::pair<std::string, std::string>> m_uses;

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

    //! Record a `use lib.pkg.all` clause so the unit can see the package
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
    ExprP parseShift();
    ExprP parseExp();
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
    void parseDeclInto(std::vector<Decl>& decls, Decl::Kind kind);
    void parseTypeDecl(Architecture& arch);
    void parseTypeDeclInto(std::vector<TypeDecl>& types);
    void parseSubtypeDecl(Architecture& arch);
    void parseSubtypeInto(std::vector<TypeDecl>& types);
    void parseAliasInto(std::vector<AliasDecl>& aliases);
    void parsePackage(FileLine* flp, bool isBody);
    void parseUseClause();
    void skipLibraryClause();
    void parseRecordBody(TypeDecl& decl);
    void parseIndexConstraint(TypeSpec& type);
    void parseArchitecture(FileLine* flp);
    void parseConcurrentStatement(Architecture& arch);
    bool parseChoices(std::vector<ExprP>& choices);
    void parseInstance(Architecture& arch, FileLine* flp, const std::string& label, bool direct);
    void parseGenericClause(EntityUnit& entity);
    void parseAssocList(std::vector<PortAssoc>& out, const char* what);
    void parseComponentDecl(Architecture& arch);
    void parseConcurrentList(std::vector<std::unique_ptr<Concurrent>>& out,
                             std::vector<Decl>* decls = nullptr,
                             std::vector<TypeDecl>* types = nullptr);
    bool parseGenerateEnd();
    void parseProcess(Architecture& arch, FileLine* flp);
    std::vector<StmtP> parseStmtList(bool stopAtElse);
    StmtP parseSequentialStatement();
    StmtP parseIf(FileLine* flp);
    StmtP parseCase(FileLine* flp);
    StmtP parseFor(FileLine* flp);
    bool parseLoopEnd(const char* what);

public:
    Parser(std::string filename, std::string text, std::string libname, bool inLibrary)
        : m_filename{filename}
        , m_lexer{m_filename, std::move(text)}
        , m_libname{std::move(libname)}
        , m_inLibrary{inLibrary} {}

    void parseDesignFile();
    std::vector<EntityUnit>& entities() { return m_entities; }
    std::vector<Architecture>& archs() { return m_archs; }
    std::vector<PackageUnit>& packages() { return m_packages; }
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
    ExprP lhsp = parseShift();
    if (!lhsp) return nullptr;
    while (isSym("=") || isSym("/=") || isSym("<") || isSym("<=") || isSym(">") || isSym(">=")
           || isSym("?=") || isSym("?/=") || isSym("?<") || isSym("?<=") || isSym("?>")
           || isSym("?>=")) {
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        ExprP rhsp = parseShift();
        if (!rhsp) return nullptr;
        node->a = std::move(lhsp);
        node->b = std::move(rhsp);
        lhsp = std::move(node);
    }
    return lhsp;
}

ExprP Parser::parseShift() {
    ExprP lhsp = parseAdd();
    if (!lhsp) return nullptr;
    while (isId("sll") || isId("srl") || isId("sla") || isId("sra") || isId("rol")
           || isId("ror")) {
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
    ExprP lhsp = parseExp();
    if (!lhsp) return nullptr;
    while (isSym("*") || isSym("/") || isId("mod") || isId("rem")) {
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = m_cur.text;
        advance();
        ExprP rhsp = parseExp();
        if (!rhsp) return nullptr;
        node->a = std::move(lhsp);
        node->b = std::move(rhsp);
        lhsp = std::move(node);
    }
    return lhsp;
}

ExprP Parser::parseExp() {
    ExprP lhsp = parseUnary();
    if (!lhsp) return nullptr;
    if (!isSym("**")) return lhsp;
    ExprP node = std::make_unique<Expr>(Expr::BINARY);
    node->flp = m_cur.flp;
    node->text = "**";
    advance();
    ExprP rhsp = parseExp();  // Right associative
    if (!rhsp) return nullptr;
    node->a = std::move(lhsp);
    node->b = std::move(rhsp);
    return node;
}

ExprP Parser::parseUnary() {
    if (isSym("+") || isSym("-") || isSym("??") || isId("abs") || isId("not")) {
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
        FileLine* const aggFlp = m_cur.flp;
        ExprP agg = std::make_unique<Expr>(Expr::AGG_POSITIONAL);
        agg->flp = aggFlp;
        for (;;) {
            ExprP item = parseExpr();
            if (!item) return nullptr;
            if (isId("to") || isId("downto")) {  // Range choice, e.g. 1 to 2
                ExprP range = std::make_unique<Expr>(Expr::SLICE);
                range->flp = item->flp;
                range->text = m_cur.text;
                advance();
                range->b = std::move(item);
                range->c = parseExpr();
                if (!range->c) return nullptr;
                item = std::move(range);
            }
            if (agg->kind == Expr::AGG_NAMED || isSym("=>") || isSym("|")) {
                // Named association: one or more choices, then the associated value
                agg->kind = Expr::AGG_NAMED;
                std::vector<ExprP> choiceList;
                choiceList.push_back(std::move(item));
                while (acceptSym("|")) {
                    ExprP more = parseExpr();
                    if (!more) return nullptr;
                    choiceList.push_back(std::move(more));
                }
                if (!acceptSym("=>")) {
                    error("expected '=>' in a named association");
                    return nullptr;
                }
                ExprP value = parseExpr();
                if (!value) return nullptr;
                agg->choices.push_back(std::move(choiceList));
                agg->args.push_back(std::move(value));
            } else {
                agg->choices.emplace_back();
                agg->args.push_back(std::move(item));
            }
            if (!acceptSym(",")) break;
        }
        expectSym(")");
        if (agg->kind == Expr::AGG_POSITIONAL && agg->args.size() == 1) {
            ExprP node = std::make_unique<Expr>(Expr::PAREN);
            node->flp = aggFlp;
            node->a = std::move(agg->args.front());
            return node;
        }
        return agg;
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
            advance();
            if (isSym("(")) {
                // Type qualification: `type_mark'(expression)`
                advance();
                ExprP operand = parseExpr();
                if (!operand) return nullptr;
                expectSym(")");
                ExprP qual = std::make_unique<Expr>(Expr::QUAL);
                qual->flp = node->flp;
                qual->text = node->text;
                qual->spelling = node->spelling;
                qual->a = std::move(operand);
                node = std::move(qual);
                continue;
            }
            Token attrTok;
            if (!takeId(attrTok)) return nullptr;
            ExprP attr = std::make_unique<Expr>(Expr::ATTR);
            attr->flp = node->flp;
            attr->text = attrTok.text;
            attr->a = std::move(node);
            if (acceptSym("(")) {  // Attribute parameter, e.g. t'pos(x)
                for (;;) {
                    ExprP arg = parseExpr();
                    if (!arg) return nullptr;
                    attr->args.push_back(std::move(arg));
                    if (!acceptSym(",")) break;
                }
                expectSym(")");
            }
            node = std::move(attr);
            continue;
        }
        if (isSym(".")) {
            advance();
            Token memberTok;
            if (!takeId(memberTok)) return nullptr;
            ExprP sel = std::make_unique<Expr>(Expr::SELECTED);
            sel->flp = node->flp;
            sel->text = memberTok.text;
            sel->spelling = memberTok.spelling;
            sel->a = std::move(node);
            node = std::move(sel);
            continue;
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
        if (m_cur.kind == Token::ID) {
            // The constraint may name a subtype instead of giving a range
            const auto saved = m_lexer.state();
            const Token ahead = m_lexer.next();
            m_lexer.restore(saved);
            if (ahead.kind == Token::SYM && ahead.text == ")") {
                Token markTok2;
                if (!takeId(markTok2)) return;
                type.constraintMark = markTok2.text;
                expectSym(")");
                return;
            }
        }
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
        // IEEE 1800-2023 17.2: an omitted port mode means `in`
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
            port.dir = PortMode::IN;
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

//! `generic (name_list : subtype_indication [:= default], ...);`
void Parser::parseGenericClause(EntityUnit& entity) {
    if (!acceptSym("(")) {
        error("expected '(' after 'generic'");
        return;
    }
    for (;;) {
        if (m_failed) return;
        if (isId("type")) {
            unsupported("generic type parameter");
            return;
        }
        GenericDecl generic;
        generic.flp = m_cur.flp;
        parseNameList(generic.names);
        if (m_failed) return;
        // IEEE 1800-2023 17.2: a generic's mode is omitted or `in`
        if (acceptId("in")) {
        } else if (isId("out") || isId("inout") || isId("buffer") || isId("linkage")) {
            unsupported("generic mode '" + m_cur.text + "'");
            return;
        }
        parseTypeSpec(generic.type);
        if (m_failed) return;
        if (acceptSym(":=")) {
            generic.init = parseExpr();
            if (!generic.init) return;
        }
        entity.generics.push_back(std::move(generic));
        if (acceptSym(";")) continue;
        if (acceptSym(")")) break;
        error("expected ';' or ')' in a generic clause, got '" + m_cur.text + "'");
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
    entity.uses = m_uses;
    if (!expectId("is")) return;
    for (;;) {
        if (m_failed) return;
        if (isId("port")) {
            advance();
            parsePortClause(entity);
            continue;
        }
        if (isId("shared")) {
            advance();
            if (!expectId("variable")) return;
            parseDeclInto(entity.decls, Decl::VARIABLE);
            continue;
        }
        if (isId("variable")) {
            advance();
            parseDeclInto(entity.decls, Decl::VARIABLE);
            continue;
        }
        if (isId("signal")) {
            advance();
            parseDeclInto(entity.decls, Decl::SIGNAL);
            continue;
        }
        if (isId("constant")) {
            advance();
            parseDeclInto(entity.decls, Decl::CONSTANT);
            continue;
        }
        if (isId("type")) {
            advance();
            parseTypeDeclInto(entity.types);
            continue;
        }
        if (isId("subtype")) {
            advance();
            parseSubtypeInto(entity.types);
            continue;
        }
        if (isId("generic")) {
            advance();
            parseGenericClause(entity);
            continue;
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

void Parser::parseDecl(Architecture& arch, Decl::Kind kind) { parseDeclInto(arch.decls, kind); }

void Parser::parseDeclInto(std::vector<Decl>& decls, Decl::Kind kind) {
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
    decls.push_back(std::move(decl));
}

//! Parse the index constraint of an array type definition
void Parser::parseIndexConstraint(TypeSpec& type) {
    if (m_cur.kind == Token::ID && !isId("to") && !isId("downto")) {
        Token markTok;
        if (!takeId(markTok)) return;
        type.mark = markTok.text;
        type.flp = markTok.flp;
        if (acceptId("range")) {
            if (acceptSym("<>")) {  // Unconstrained index
                type.hasRange = true;
                return;
            }
            parseRange(type);
            return;
        }
        if (isSym("(")) {  // Index constrained by a subtype indication
            advance();
            parseRange(type);
            expectSym(")");
        }
        return;
    }
    parseRange(type);
}

void Parser::parseRecordBody(TypeDecl& decl) {
    for (;;) {
        if (m_failed) return;
        if (isId("end")) break;
        TypeDecl::Member member;
        parseNameList(member.names);
        if (m_failed) return;
        parseTypeSpec(member.type);
        if (m_failed) return;
        if (!acceptSym(";")) {
            error("expected ';' after a record element declaration");
            return;
        }
        decl.members.push_back(std::move(member));
    }
    if (!expectId("end")) return;
    if (!expectId("record")) return;
    if (m_cur.kind == Token::ID) advance();  // Optional end name
    if (!acceptSym(";")) error("expected ';' after 'end record'");
}

void Parser::parseTypeDecl(Architecture& arch) { parseTypeDeclInto(arch.types); }

void Parser::parseTypeDeclInto(std::vector<TypeDecl>& types) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    TypeDecl decl{TypeDecl::ARRAY};
    decl.flp = nameTok.flp;
    decl.name = nameTok.text;
    decl.spelling = nameTok.spelling;
    if (!expectId("is")) return;
    if (isSym("(")) {  // Enumeration
        decl.kind = TypeDecl::ENUM;
        advance();
        for (;;) {
            Token literalTok;
            if (!takeId(literalTok)) return;
            addName(decl.literals, literalTok);
            if (!acceptSym(",")) break;
        }
        expectSym(")");
    } else if (isId("record")) {
        decl.kind = TypeDecl::RECORD;
        advance();
        parseRecordBody(decl);
        if (m_failed) return;
        types.push_back(std::move(decl));
        return;
    } else if (isId("array")) {
        decl.kind = TypeDecl::ARRAY;
        advance();
        if (!acceptSym("(")) {
            error("expected '(' after 'array'");
            return;
        }
        parseIndexConstraint(decl.index);
        if (m_failed) return;
        expectSym(")");
        if (!expectId("of")) return;
        parseTypeSpec(decl.base);
        if (m_failed) return;
    } else {
        unsupported("type definition '" + m_cur.text + "'");
        return;
    }
    if (!acceptSym(";")) {
        error("expected ';' after a type declaration");
        return;
    }
    types.push_back(std::move(decl));
}

void Parser::parseSubtypeDecl(Architecture& arch) { parseSubtypeInto(arch.types); }

void Parser::parseSubtypeInto(std::vector<TypeDecl>& types) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    TypeDecl decl{TypeDecl::SUBTYPE};
    decl.flp = nameTok.flp;
    decl.name = nameTok.text;
    decl.spelling = nameTok.spelling;
    if (!expectId("is")) return;
    parseTypeSpec(decl.base);
    if (m_failed) return;
    if (!acceptSym(";")) {
        error("expected ';' after a subtype declaration");
        return;
    }
    types.push_back(std::move(decl));
}

//! Parse `alias name [: mark] is designator;`
void Parser::parseAliasInto(std::vector<AliasDecl>& aliases) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    AliasDecl alias;
    alias.flp = nameTok.flp;
    alias.name = nameTok.text;
    alias.spelling = nameTok.spelling;
    if (acceptSym(":")) parseTypeSpec(alias.mark);  // Mark with its index constraint
    if (!expectId("is")) return;
    alias.designator = parseDesignator();
    if (!alias.designator) return;
    if (!acceptSym(";")) {
        error("expected ';' after an alias declaration");
        return;
    }
    aliases.push_back(std::move(alias));
}

//! `component name [is] [generic (...);] [port (...);] end component [name];`
void Parser::parseComponentDecl(Architecture& arch) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    (void)acceptId("is");
    for (;;) {
        if (m_failed) return;
        if (isId("end")) break;
        if (isId("generic") || isId("port")) {
            // The interface list only shapes the association checks; skip it
            advance();
            int depth = 0;
            while (!m_failed && m_cur.kind != Token::END) {
                if (isSym("(")) ++depth;
                if (isSym(")")) --depth;
                const bool done = depth == 0 && isSym(";");
                advance();
                if (done) break;
            }
            continue;
        }
        unsupported("component declaration item '" + m_cur.text + "'");
        return;
    }
    if (!expectId("end")) return;
    if (!expectId("component")) return;
    if (m_cur.kind == Token::ID) advance();  // Optional end name
    if (!acceptSym(";")) error("expected ';' after 'end component'");
    arch.components.push_back(nameTok.text);
}

//! `[entity] name [generic map (...)] [port map (...)];`
void Parser::parseInstance(Architecture& arch, FileLine* flp, const std::string& label,
                           bool direct) {
    auto inst = std::make_unique<Concurrent>(Concurrent::INSTANCE);
    inst->flp = flp;
    inst->instanceLabel = label.empty() ? "inst" : label;
    // The entity may be library qualified: `work.child`
    Token nameTok;
    if (!takeId(nameTok)) return;
    std::string name = nameTok.text;
    while (acceptSym(".")) {
        Token partTok;
        if (!takeId(partTok)) return;
        name = partTok.text;  // The library is not modelled
    }
    inst->instanceName = name;
    (void)direct;
    for (;;) {
        if (isId("generic")) {
            advance();
            if (!expectId("map")) return;
            parseAssocList(inst->genericAssociations, "generic");
            if (m_failed) return;
            continue;
        }
        if (isId("port")) {
            advance();
            if (!expectId("map")) return;
            parseAssocList(inst->associations, "port");
            if (m_failed) return;
            continue;
        }
        break;
    }
    if (!acceptSym(";")) {
        error("expected ';' after an instantiation");
        return;
    }
    arch.stmts.push_back(std::move(inst));
}

//! `(formal [range] => actual, ...)` associations of a port or generic map
void Parser::parseAssocList(std::vector<PortAssoc>& out, const char* what) {
    if (!acceptSym("(")) {
        error(std::string{"expected '(' after '"} + what + " map'");
        return;
    }
    for (;;) {
        if (m_failed) return;
        PortAssoc assoc;
        assoc.flp = m_cur.flp;
        // Look ahead for `formal [range] =>`: a positional actual may look similar
        bool named = false;
        {
            const Token savedTok = m_cur;
            const auto savedState = m_lexer.state();
            if (m_cur.kind == Token::ID) {
                advance();
                if (isSym("(")) {
                    int depth = 0;
                    do {
                        if (isSym("(")) ++depth;
                        if (isSym(")")) --depth;
                        advance();
                    } while (!m_failed && depth > 0 && m_cur.kind != Token::END);
                }
                named = isSym("=>");
            }
            m_cur = savedTok;
            m_lexer.restore(savedState);
        }
        if (named) {
            Token formalTok;
            if (!takeId(formalTok)) return;
            assoc.formal = formalTok.text;
            assoc.formalSpelling = formalTok.spelling;
            if (isSym("(")) {  // The association targets part of the port
                advance();
                assoc.formalLeft = parseExpr();
                if (!assoc.formalLeft) return;
                if (isId("to") || isId("downto")) {
                    assoc.formalAscending = isId("to");
                    advance();
                    assoc.formalRight = parseExpr();
                    if (!assoc.formalRight) return;
                }
                expectSym(")");
                assoc.hasFormalRange = true;
            }
            if (!acceptSym("=>")) {
                error(std::string{"expected '=>' in a "} + what + " association");
                return;
            }
            if (isId("open")) {
                advance();
            } else {
                assoc.actual = parseExpr();
                if (!assoc.actual) return;
            }
        } else {
            // Positional association: the actual maps to the next item in order
            assoc.actual = parseExpr();
            if (!assoc.actual) return;
        }
        out.push_back(std::move(assoc));
        if (!acceptSym(",")) break;
    }
    expectSym(")");
}

void Parser::parseArchitecture(FileLine* flp) {
    Token nameTok;
    if (!takeId(nameTok)) return;
    Architecture arch;
    arch.flp = flp;
    arch.name = nameTok.text;
    arch.libname = m_libname;
    arch.inLibrary = m_inLibrary;
    arch.uses = m_uses;
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
        if (isId("default") || isId("attribute") || isId("disconnect")) {
            // Verification directives and attribute specifications have no synthesis meaning
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
        if (isId("type")) {
            advance();
            parseTypeDecl(arch);
            continue;
        }
        if (isId("subtype")) {
            advance();
            parseSubtypeDecl(arch);
            continue;
        }
        if (isId("variable")) {
            advance();
            parseDecl(arch, Decl::VARIABLE);
            continue;
        }
        if (isId("shared")) {
            advance();
            if (!expectId("variable")) return;
            parseDecl(arch, Decl::VARIABLE);
            continue;
        }
        if (isId("alias")) {
            advance();
            parseAliasInto(arch.aliases);
            continue;
        }
        if (isId("component")) {
            advance();
            parseComponentDecl(arch);
            continue;
        }
        if (isId("attribute") || isId("function") || isId("procedure") || isId("package")) {
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
    // Optional statement label: `label : statement`
    std::string label;
    if (m_cur.kind == Token::ID) {
        const auto saved = m_lexer.state();
        const Token ahead = m_lexer.next();
        m_lexer.restore(saved);
        if (ahead.kind == Token::SYM && ahead.text == ":") {
            label = m_cur.text;
            advance();
            advance();
        }
    }
    if (isId("entity")) {
        advance();
        parseInstance(arch, flp, label, true);
        return;
    }
    if (m_cur.kind == Token::ID) {
        // `label : child port map (...)`: a component or entity instantiation
        const auto saved = m_lexer.state();
        const Token ahead = m_lexer.next();
        m_lexer.restore(saved);
        if (ahead.kind == Token::ID && (ahead.text == "port" || ahead.text == "generic")
            && m_cur.text != "process") {
            parseInstance(arch, flp, label, false);
            return;
        }
    }
    if (!label.empty()
        && (isId("block") || isId("process") || isId("for") || isId("if") || isId("with"))) {
        // The label belongs to the statement itself; fall through to its parser
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
    if (isId("for")) {
        advance();
        Token varTok;
        if (!takeId(varTok)) return;
        auto gen = std::make_unique<Concurrent>(Concurrent::GEN_FOR);
        gen->flp = flp;
        gen->loopVar = varTok.text;
        if (!expectId("in")) return;
        gen->lo = parseExpr();
        if (!gen->lo) return;
        if (isId("to") || isId("downto")) {
            gen->rangeAscending = isId("to");
            advance();
            gen->hi = parseExpr();
            if (!gen->hi) return;
        } else if (!isId("generate")) {
            error("expected 'to' or 'downto' in a generate range");
            return;
        }
        if (!expectId("generate")) return;
        parseConcurrentList(gen->body, &gen->decls, &gen->types);
        if (m_failed) return;
        if (!parseGenerateEnd()) return;
        arch.stmts.push_back(std::move(gen));
        return;
    }
    if (isId("if")) {
        advance();
        auto gen = std::make_unique<Concurrent>(Concurrent::GEN_IF);
        gen->flp = flp;
        gen->cond = parseExpr();
        if (!gen->cond) return;
        if (!expectId("generate")) return;
        parseConcurrentList(gen->body, &gen->decls, &gen->types);
        if (m_failed) return;
        if (acceptId("else")) {
            if (!expectId("generate")) return;
            parseConcurrentList(gen->elseBody, &gen->decls, &gen->types);
            if (m_failed) return;
        }
        if (!parseGenerateEnd()) return;
        arch.stmts.push_back(std::move(gen));
        return;
    }
    if (isId("block")) {
        advance();
        auto block = std::make_unique<Concurrent>(Concurrent::BLOCK);
        block->flp = flp;
        if (isSym("(")) {  // Guarded block: the guard only names a condition
            unsupported("guarded block statement");
            return;
        }
        (void)acceptId("is");
        while (!m_failed && !isId("begin") && !isId("end")) {
            if (isId("signal")) {
                advance();
                parseDeclInto(block->decls, Decl::SIGNAL);
                continue;
            }
            if (isId("shared")) {
                advance();
                if (!expectId("variable")) return;
                parseDeclInto(block->decls, Decl::VARIABLE);
                continue;
            }
            if (isId("variable")) {
                advance();
                parseDeclInto(block->decls, Decl::VARIABLE);
                continue;
            }
            if (isId("constant")) {
                advance();
                parseDeclInto(block->decls, Decl::CONSTANT);
                continue;
            }
            if (isId("type")) {
                advance();
                parseTypeDeclInto(block->types);
                continue;
            }
            if (isId("subtype")) {
                advance();
                parseSubtypeInto(block->types);
                continue;
            }
            if (isId("use")) {
                skipContextClause();
                continue;
            }
            unsupported("block declarative item '" + m_cur.text + "'");
            return;
        }
        if (acceptId("begin")) {
            parseConcurrentList(block->body);
            if (m_failed) return;
        }
        if (!expectId("end")) return;
        (void)acceptId("block");
        if (m_cur.kind == Token::ID) advance();  // Optional end label
        if (!acceptSym(";")) error("expected ';' after 'end block'");
        arch.stmts.push_back(std::move(block));
        return;
    }
    if (isId("assert")) {
        advance();
        auto conc = std::make_unique<Concurrent>(Concurrent::ASSERT);
        conc->flp = flp;
        conc->target = parseExpr();  // The condition
        if (!conc->target) return;
        if (acceptId("report")) {
            conc->value = parseExpr();
            if (!conc->value) return;
        }
        if (acceptId("severity")) {
            Token sevTok;
            if (!takeId(sevTok)) return;
            (void)sevTok;  // Synthesis ignores the severity
        }
        if (!acceptSym(";")) error("expected ';' after 'assert'");
        arch.stmts.push_back(std::move(conc));
        return;
    }
    if (isId("entity") || isId("component") || isId("case")) {
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

//! Parse concurrent statements up to `end [generate|block]`, without the terminator
void Parser::parseConcurrentList(std::vector<std::unique_ptr<Concurrent>>& out,
                                 std::vector<Decl>* decls, std::vector<TypeDecl>* types) {
    Architecture scratch;  // The statements of a generate body
    while (!m_failed && !isId("end") && !isId("elsif") && !isId("else")
           && m_cur.kind != Token::END) {
        // A generate body separates its declarative part from its statements
        if (isId("begin")) {
            advance();
            continue;
        }
        // A generate body may carry declarative items before its statements
        if (decls && isId("signal")) {
            advance();
            parseDeclInto(*decls, Decl::SIGNAL);
            continue;
        }
        if (decls && isId("shared")) {
            advance();
            if (!expectId("variable")) return;
            parseDeclInto(*decls, Decl::VARIABLE);
            continue;
        }
        if (decls && isId("variable")) {
            advance();
            parseDeclInto(*decls, Decl::VARIABLE);
            continue;
        }
        if (decls && isId("constant")) {
            advance();
            parseDeclInto(*decls, Decl::CONSTANT);
            continue;
        }
        if (types && isId("type")) {
            advance();
            parseTypeDeclInto(*types);
            continue;
        }
        if (types && isId("subtype")) {
            advance();
            parseSubtypeInto(*types);
            continue;
        }
        if (decls && isId("alias")) {
            advance();
            parseAliasInto(scratch.aliases);
            continue;
        }
        parseConcurrentStatement(scratch);
    }
    out = std::move(scratch.stmts);
}

//! Consume `end [generate] [label];`
bool Parser::parseGenerateEnd() {
    if (!expectId("end")) return false;
    (void)acceptId("generate");
    if (m_cur.kind == Token::ID) advance();  // Optional end label
    if (!acceptSym(";")) {
        error("expected ';' after 'end generate'");
        return false;
    }
    return true;
}

void Parser::parseProcess(Architecture& arch, FileLine* flp) {
    Process process;
    process.flp = flp;
    if (acceptSym("(")) {
        process.hasSensitivity = true;
        if (isId("all")) {
            advance();
            process.sensitivityAll = true;
            expectSym(")");
        } else {
            for (;;) {
                Token nameTok;
                if (!takeId(nameTok)) return;
                process.sensitivity.push_back(nameTok.text);
                if (!acceptSym(",")) break;
            }
            expectSym(")");
        }
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
        if (isId("variable")) {
            advance();
            parseDeclInto(process.decls, Decl::VARIABLE);
            continue;
        }
        if (isId("shared")) {
            advance();
            if (!expectId("variable")) return;
            parseDeclInto(process.decls, Decl::VARIABLE);
            continue;
        }
        if (isId("subtype")) {
            advance();
            parseSubtypeInto(process.types);
            continue;
        }
        if (isId("type")) {
            advance();
            parseTypeDeclInto(process.types);
            continue;
        }
        if (isId("constant")) {
            advance();
            parseDeclInto(process.decls, Decl::CONSTANT);
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
        if (isId("when")) break;  // Start of the next case alternative
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
    // Optional statement label: `label : statement`
    std::string label;
    if (m_cur.kind == Token::ID) {
        const auto saved = m_lexer.state();
        const Token ahead = m_lexer.next();
        m_lexer.restore(saved);
        if (ahead.kind == Token::SYM && ahead.text == ":") {
            label = m_cur.text;
            advance();
            advance();
        }
    }
    if (isId("if")) {
        advance();
        return parseIf(flp);
    }
    if (isId("case")) {
        advance();
        return parseCase(flp);
    }
    if (isId("for")) {
        advance();
        StmtP stmt = parseFor(flp);
        if (stmt) stmt->loopLabel = label;
        return stmt;
    }
    if (isId("while")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::WHILE);
        stmt->flp = flp;
        stmt->loopLabel = label;
        stmt->value = parseExpr();  // Loop condition
        if (!stmt->value) return nullptr;
        if (!expectId("loop")) return nullptr;
        stmt->body = parseStmtList(false);
        if (m_failed) return nullptr;
        if (!parseLoopEnd("while")) return nullptr;
        return stmt;
    }
    if (isId("loop")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::LOOP);
        stmt->flp = flp;
        stmt->loopLabel = label;
        stmt->body = parseStmtList(false);
        if (m_failed) return nullptr;
        if (!parseLoopEnd("loop")) return nullptr;
        return stmt;
    }
    if (isId("exit") || isId("next")) {
        const bool isExit = isId("exit");
        advance();
        auto stmt = std::make_unique<Stmt>(isExit ? Stmt::EXIT : Stmt::NEXT);
        stmt->flp = flp;
        if (m_cur.kind == Token::ID && !isId("when")) {
            Token labelTok;
            if (!takeId(labelTok)) return nullptr;
            stmt->loopLabel = labelTok.text;
        }
        if (acceptId("when")) {
            stmt->value = parseExpr();
            if (!stmt->value) return nullptr;
        }
        expectSym(";");
        return stmt;
    }
    if (isId("wait")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::WAIT);
        stmt->flp = flp;
        if (acceptId("until")) {
            stmt->value = parseExpr();
            if (!stmt->value) return nullptr;
        } else if (acceptId("on")) {
            for (;;) {
                Token nameTok;
                if (!takeId(nameTok)) return nullptr;
                stmt->waitNames.push_back(nameTok.text);
                if (!acceptSym(",")) break;
            }
        } else if (isId("for")) {
            unsupported("wait for a delay");
            return nullptr;
        } else {
            stmt->waitForever = true;
        }
        expectSym(";");
        return stmt;
    }
    if (isId("report")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::REPORT);
        stmt->flp = flp;
        stmt->value = parseExpr();  // The report message; only its syntax matters
        if (!stmt->value) return nullptr;
        if (acceptId("severity")) { advance(); }
        expectSym(";");
        return stmt;
    }
    if (isId("assert")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::REPORT);
        stmt->flp = flp;
        stmt->value = parseExpr();
        if (!stmt->value) return nullptr;
        if (acceptId("report")) {
            ExprP message = parseExpr();
            if (!message) return nullptr;
        }
        if (acceptId("severity")) advance();
        expectSym(";");
        return stmt;
    }
    if (isId("return")) {
        unsupported("sequential statement 'return'");
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
    if (acceptSym(":=")) {
        auto stmt = std::make_unique<Stmt>(Stmt::VAR_ASSIGN);
        stmt->flp = flp;
        stmt->target = std::move(target);
        stmt->value = parseExpr();
        if (!stmt->value) return nullptr;
        if (isId("when") || isId("after")) {
            unsupported("conditional or delayed variable assignment");
            return nullptr;
        }
        expectSym(";");
        return stmt;
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

//! Record `use lib.pkg[.item];`; only whole-package uses make names visible
void Parser::parseUseClause() {
    advance();  // 'use'
    std::vector<std::string> parts;
    for (;;) {
        if (m_cur.kind != Token::ID) break;
        parts.push_back(m_cur.text);
        advance();
        if (!acceptSym(".")) break;
    }
    expectSym(";");
    if (parts.size() >= 3 && parts.back() == "all") m_uses.emplace_back(parts[0], parts[1]);
}

//! Skip `library <name>;`, which only affects which library GHDL would search
void Parser::skipLibraryClause() {
    while (!m_failed && m_cur.kind != Token::END && !isSym(";")) advance();
    expectSym(";");
}

void Parser::parsePackage(FileLine* flp, bool isBody) {
    if (isBody && !expectId("body")) return;
    Token nameTok;
    if (!takeId(nameTok)) return;
    PackageUnit pkg;
    pkg.flp = flp;
    pkg.name = nameTok.text;
    pkg.spelling = nameTok.spelling;
    pkg.libname = m_libname;
    pkg.inLibrary = m_inLibrary;
    pkg.isBody = isBody;
    if (!expectId("is")) return;
    for (;;) {
        if (m_failed) return;
        if (isId("end")) break;
        if (isId("use")) {
            parseUseClause();
            continue;
        }
        if (isId("constant")) {
            advance();
            parseDeclInto(pkg.decls, Decl::CONSTANT);
            continue;
        }
        if (isId("type")) {
            advance();
            parseTypeDeclInto(pkg.types);
            continue;
        }
        if (isId("subtype")) {
            advance();
            parseSubtypeInto(pkg.types);
            continue;
        }
        if (isId("alias")) {
            advance();
            parseAliasInto(pkg.aliases);
            continue;
        }
        if (isId("function") || isId("procedure") || isId("component") || isId("attribute")
            || isId("signal") || isId("shared") || isId("variable") || isId("file")
            || isId("disconnect") || isId("group")) {
            unsupported("package declarative item '" + m_cur.text + "'");
            return;
        }
        error("unexpected package declaration '" + m_cur.text + "'");
        return;
    }
    if (!expectId("end")) return;
    (void)acceptId("package");
    if (isBody) (void)acceptId("body");
    if (m_cur.kind == Token::ID) {
        Token endTok;
        if (!takeId(endTok)) return;
        if (endTok.text != pkg.name) {
            error(flp, "package end name '" + endTok.spelling + "' does not match '" + pkg.spelling
                           + "'");
            return;
        }
    }
    if (!acceptSym(";")) {
        error("expected ';' after 'end' of package");
        return;
    }
    m_packages.push_back(std::move(pkg));
}

StmtP Parser::parseCase(FileLine* flp) {
    auto stmt = std::make_unique<Stmt>(Stmt::CASE);
    stmt->flp = flp;
    stmt->selector = parseExpr();
    if (!stmt->selector) return nullptr;
    if (!expectId("is")) return nullptr;
    while (!m_failed && !isId("end")) {
        if (m_cur.kind == Token::END) {
            error("unexpected end of file in a case statement");
            return nullptr;
        }
        if (!expectId("when")) return nullptr;
        std::vector<ExprP> choices;
        if (!parseChoices(choices)) return nullptr;
        expectSym("=>");
        std::vector<StmtP> body = parseStmtList(true);
        if (m_failed) return nullptr;
        stmt->caseItems.emplace_back(std::move(choices), std::move(body));
    }
    if (!expectId("end")) return nullptr;
    if (!expectId("case")) return nullptr;
    if (m_cur.kind == Token::ID) advance();  // Optional end label
    if (!acceptSym(";")) {
        error("expected ';' after 'end case'");
        return nullptr;
    }
    return stmt;
}

StmtP Parser::parseFor(FileLine* flp) {
    Token varTok;
    if (!takeId(varTok)) return nullptr;
    auto stmt = std::make_unique<Stmt>(Stmt::FOR);
    stmt->flp = flp;
    stmt->loopVar = varTok.text;
    if (!expectId("in")) return nullptr;
    stmt->b = parseExpr();
    if (!stmt->b) return nullptr;
    if (isId("to") || isId("downto")) {
        stmt->rangeAscending = isId("to");
        advance();
        stmt->c = parseExpr();
        if (!stmt->c) return nullptr;
    } else if (!isId("loop")) {
        // Only `for i in <range attribute> loop` may omit the direction
        error("expected 'to' or 'downto' in a loop range");
        return nullptr;
    }
    if (!expectId("loop")) return nullptr;
    stmt->body = parseStmtList(false);
    if (m_failed) return nullptr;
    if (!parseLoopEnd("for")) return nullptr;
    return stmt;
}

//! Consume `end loop [label];`
bool Parser::parseLoopEnd(const char* what) {
    if (!expectId("end")) return false;
    if (!expectId("loop")) {
        error(std::string{"expected 'end loop' to close the "} + what + " statement");
        return false;
    }
    if (m_cur.kind == Token::ID) advance();  // Optional end label
    if (!acceptSym(";")) {
        error("expected ';' after 'end loop'");
        return false;
    }
    return true;
}

void Parser::parseDesignFile() {
    advance();
    while (!m_failed && m_cur.kind != Token::END) {
        if (isId("use")) {
            parseUseClause();
            continue;
        }
        if (isId("library")) {
            advance();
            skipLibraryClause();
            continue;
        }
        if (isId("context")) {
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
        if (isId("package")) {
            FileLine* const flp = m_cur.flp;
            advance();
            parsePackage(flp, isId("body"));
            continue;
        }
        if (isId("configuration")) {
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
    for (PackageUnit& pkg : parser.packages()) library.packages.push_back(std::move(pkg));
}

}  // namespace vhdlnative
