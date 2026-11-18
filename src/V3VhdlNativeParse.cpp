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

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

//! The VHDL reserved words, IEEE 1076-2008 15.10. A physical literal writes its unit as an
//! identifier, and nothing else in the grammar puts an identifier directly after an abstract
//! literal, so a reserved word in that position -- `1 to 10`, `0 sec then`, `5 mod 2` -- names
//! no unit. The predefined names (min, now, ...) are not reserved and may name a unit.
bool isReservedWord(const std::string& folded) {
    static const char* const words[] = {"abs",
                                        "access",
                                        "after",
                                        "alias",
                                        "all",
                                        "and",
                                        "architecture",
                                        "array",
                                        "assert",
                                        "assume",
                                        "assume_guarantee",
                                        "attribute",
                                        "begin",
                                        "block",
                                        "body",
                                        "buffer",
                                        "bus",
                                        "case",
                                        "component",
                                        "configuration",
                                        "constant",
                                        "context",
                                        "cover",
                                        "default",
                                        "disconnect",
                                        "downto",
                                        "else",
                                        "elsif",
                                        "end",
                                        "entity",
                                        "exit",
                                        "fairness",
                                        "file",
                                        "for",
                                        "force",
                                        "function",
                                        "generate",
                                        "generic",
                                        "group",
                                        "guarded",
                                        "if",
                                        "impure",
                                        "in",
                                        "inertial",
                                        "inout",
                                        "is",
                                        "label",
                                        "library",
                                        "linkage",
                                        "literal",
                                        "loop",
                                        "map",
                                        "mod",
                                        "nand",
                                        "new",
                                        "next",
                                        "nor",
                                        "not",
                                        "null",
                                        "of",
                                        "on",
                                        "open",
                                        "or",
                                        "others",
                                        "out",
                                        "package",
                                        "parameter",
                                        "port",
                                        "postponed",
                                        "procedure",
                                        "process",
                                        "property",
                                        "protected",
                                        "pure",
                                        "range",
                                        "record",
                                        "register",
                                        "reject",
                                        "release",
                                        "rem",
                                        "report",
                                        "restrict",
                                        "restrict_guarantee",
                                        "return",
                                        "rol",
                                        "ror",
                                        "select",
                                        "sequence",
                                        "severity",
                                        "shared",
                                        "signal",
                                        "sla",
                                        "sll",
                                        "sra",
                                        "srl",
                                        "strong",
                                        "subtype",
                                        "then",
                                        "to",
                                        "transport",
                                        "type",
                                        "unaffected",
                                        "units",
                                        "until",
                                        "use",
                                        "variable",
                                        "vmode",
                                        "vprop",
                                        "vunit",
                                        "wait",
                                        "when",
                                        "while",
                                        "with",
                                        "xnor",
                                        "xor"};
    for (const char* const word : words) {
        if (folded == word) return true;
    }
    return false;
}

//######################################################################
// Parser

class Parser final {
    const std::string m_filename;  // File being parsed
    Lexer m_lexer;  // Token source
    Token m_cur;  // Current token
    bool m_failed = false;  // Stop after the first error
    int m_stringContext = 0;  // Parsing a report message, which is a string
                              // whatever its characters are
    std::string m_libname;  // Library of the current file
    bool m_inLibrary = false;  // Current file came from a library
    std::vector<EntityUnit> m_entities;  // Parsed design units
    std::vector<ConfigurationUnit> m_configurations;  // Parsed configurations
    std::vector<Architecture> m_archs;
    std::vector<PackageUnit> m_packages;  // Parsed package specs and bodies
    std::vector<PackageInstance> m_packageInstances;  // Parsed generic-package instances
    std::vector<ContextUnit> m_contexts;  // Parsed context declarations
    // Packages named by the context clauses seen so far
    std::vector<UseClause> m_uses;  // Use clauses seen so far
    // Context declarations named by the context clauses seen so far
    std::vector<std::string> m_contextNames;

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
    //! An identifier that becomes a declared name: a reserved word is never one
    //! (IEEE 1076-2008 15.10), unlike an attribute name or a choice keyword
    bool takeName(Token& out) {
        if (!takeId(out)) return false;
        if (!isReservedWord(out.text)) return true;
        error("'" + out.text + "' is a reserved word, not an identifier");
        return false;
    }
    //! A subprogram may be named by an operator symbol, which the lexer reports as a
    //! string token. Only a subprogram may: a keyword can never be an identifier, so
    //! every other designator stays a plain ID.
    bool takeDesignator(Token& out) {
        if (m_cur.kind != Token::ID && m_cur.kind != Token::STRING) {
            error("expected a subprogram name, got '" + m_cur.text + "'");
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

    //! Skip an attribute, default or disconnect specification, or a PSL directive
    void skipContextClause() { skipToSemicolon(); }

    //! Parse `context lib.name [is ... end context];`. With a body, record the
    //! declaration; without one, add the name to the context references.
    void parseContextClause(std::vector<std::string>& names, bool allowDeclaration) {
        const Token contextTok = m_cur;
        advance();  // `context`
        std::vector<std::string> parts;
        if (m_cur.kind != Token::ID) {
            error("expected a context name");
            return;
        }
        parts.push_back(m_cur.text);
        advance();
        while (acceptSym(".")) {
            if (m_cur.kind != Token::ID) {
                error("expected a context name after '.'");
                return;
            }
            parts.push_back(m_cur.text);
            advance();
        }
        if (isId("is")) {
            if (!allowDeclaration) {
                unsupported("context declaration in a declarative part");
                return;
            }
            ContextUnit context;
            context.flp = contextTok.flp;
            context.name = parts.back();
            advance();  // `is`
            for (;;) {
                if (m_failed) return;
                if (m_cur.kind == Token::END) {
                    unsupported("context declaration that reaches the end of the file");
                    return;
                }
                if (isId("end")) {
                    advance();
                    if (!expectId("context")) return;
                    if (m_cur.kind == Token::ID) advance();  // Optional end name
                    expectSym(";");
                    break;
                }
                if (isId("library")) {
                    advance();
                    skipLibraryClause();
                    continue;
                }
                if (isId("use")) {
                    parseUseClauseInto(context.uses);
                    continue;
                }
                if (isId("context")) {
                    std::vector<std::string> referenced;
                    parseContextReference(referenced);
                    if (!referenced.empty()) context.contextNames.push_back(referenced.front());
                    continue;
                }
                error("unexpected token '" + m_cur.text + "' in a context declaration");
                return;
            }
            m_contexts.push_back(std::move(context));
            return;
        }
        expectSym(";");
        names.push_back(parts.back());
    }

    //! Parse `context lib.name;` inside a context declaration
    void parseContextReference(std::vector<std::string>& names) {
        advance();  // `context`
        std::string name;
        if (m_cur.kind != Token::ID) {
            error("expected a context name");
            return;
        }
        name = m_cur.text;
        advance();
        while (acceptSym(".")) {
            if (m_cur.kind != Token::ID) {
                error("expected a context name after '.'");
                return;
            }
            name = m_cur.text;
            advance();
        }
        expectSym(";");
        names.push_back(name);
    }

    //! Skip a declaration whose meaning is only verification-time or unused
    void skipToSemicolon() {
        int parens = 0;
        while (m_cur.kind != Token::END) {
            if (isSym("(")) ++parens;
            if (isSym(")")) --parens;
            if (parens == 0 && acceptSym(";")) return;
            advance();
        }
        unsupported("declaration that reaches the end of the file");
    }

    //! Consume a `file` declaration: a file has no synthesis meaning, so it is skipped
    bool skipFileDecl() {
        if (!isId("file")) return false;
        advance();
        skipToSemicolon();
        return true;
    }

    //! Consume up to and including the next balanced { ... } block (PSL directive bodies)
    void skipToBraceBlock() {
        while (m_cur.kind != Token::END && !isSym("{")) advance();
        int depth = 0;
        while (m_cur.kind != Token::END) {
            if (isSym("{")) {
                ++depth;
            } else if (isSym("}")) {
                if (--depth == 0) {
                    advance();
                    return;
                }
            }
            advance();
        }
    }

    //! Whether the 'assert' at m_cur is a PSL assertion (`[severity X] <psl-op> ...`)
    bool looksLikePslAssertion() {
        const auto saved = m_lexer.state();
        const Token a = m_lexer.next();
        const Token c = m_lexer.next();
        const Token d = m_lexer.next();
        m_lexer.restore(saved);
        const auto prop = [](const Token& t) { return t.kind == Token::SYM && t.text == "{"; };
        const auto kw = [](const Token& t) {
            return t.kind == Token::ID
                   && (t.text == "always" || t.text == "never" || t.text == "eventually"
                       || t.text == "nexttime" || t.text == "until" || t.text == "abort"
                       || t.text == "implies" || t.text == "iff" || t.text == "next"
                       || t.text == "prev" || t.text == "back" || t.text == "rose"
                       || t.text == "fell" || t.text == "stable" || t.text == "changed");
        };
        if (prop(a) || kw(a)) return true;
        if (a.kind == Token::ID && a.text == "severity") return prop(d) || kw(d);
        (void)c;
        return false;
    }

    //! Skip to the next ';' at depth 0 over (), [] and {} (PSL statement bodies nest ';')
    void skipToStatementEnd() {
        int depth = 0;
        while (m_cur.kind != Token::END) {
            if (isSym("(") || isSym("{") || isSym("[")) {
                ++depth;
            } else if (isSym(")") || isSym("}") || isSym("]")) {
                --depth;
            } else if (depth <= 0 && isSym(";")) {
                advance();
                return;
            }
            advance();
        }
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
    //! Consume `severity <note|warning|error|failure>`, which decides if the run stops
    Stmt::Severity parseSeverityValue();
    void parseSeverity(Stmt& stmt);
    void parseTypeSpec(TypeSpec& type);
    //! The `name(range)` list of a record constraint
    bool parseMemberConstraints(TypeSpec& type);
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
    void parseFileDeclInto(std::vector<Decl>& decls);
    void parsePackage(FileLine* flp, bool isBody);
    void parseUseClause();
    void parseUseClauseInto(std::vector<UseClause>& uses);
    void skipLibraryClause();
    void parseRecordBody(TypeDecl& decl);
    void parseIndexConstraint(TypeSpec& type);
    void parseArchitecture(FileLine* flp);
    void parseConfiguration(FileLine* flp);
    void parseConcurrentStatement(Architecture& arch);
    ExprP parseChoice();
    bool parseChoices(std::vector<ExprP>& choices);
    void parseInstance(Architecture& arch, FileLine* flp, const std::string& label, bool direct);
    void parseGenericClauseInto(std::vector<GenericDecl>& generics);
    void parsePackageInstanceTail(FileLine* flp, const std::string& name,
                                  const std::string& spelling);
    void parseAssocList(std::vector<PortAssoc>& out, const char* what);
    void parseComponentDecl();
    void parseInterfaceList(std::vector<FormalDecl>& formals);
    void parseSubprogramInto(std::vector<SubprogramDecl>& out);
    bool parseSubprogramSpec(SubprogramDecl& sub);
    void parseConcurrentList(std::vector<std::unique_ptr<Concurrent>>& out,
                             std::vector<Decl>* decls = nullptr,
                             std::vector<TypeDecl>* types = nullptr,
                             std::vector<SubprogramDecl>* subprograms = nullptr);
    bool parseGenerateEnd();
    void parseProcess(Architecture& arch, FileLine* flp);
    std::vector<StmtP> parseStmtList(bool stopAtElse);
    StmtP parseSequentialStatement();
    ExprP parseWaveformValue();
    bool parseConditionalTail(Stmt& stmt, bool allowUnaffected);
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
    std::vector<PackageInstance>& packageInstances() { return m_packageInstances; }
    std::vector<ConfigurationUnit>& configurations() { return m_configurations; }
    std::vector<ContextUnit>& contexts() { return m_contexts; }
};

//######################################################################
// Parser

ExprP Parser::parseExpr() { return parseLogical(); }

ExprP Parser::parseLogical() {
    ExprP lhsp = parseRelation();
    if (!lhsp) return nullptr;
    std::string previousOp;  // Operator of the unparenthesized chain parsed so far
    while (m_cur.kind == Token::ID
           && (m_cur.text == "and" || m_cur.text == "or" || m_cur.text == "xor"
               || m_cur.text == "nand" || m_cur.text == "nor" || m_cur.text == "xnor")) {
        const std::string op = m_cur.text;
        // IEEE 1076-2008 9.1: operators of one precedence level may not be mixed, and
        // nand/nor are non-associative, so their chains need parentheses
        if (!previousOp.empty()) {
            const bool nonAssoc
                = previousOp == "nand" || previousOp == "nor" || op == "nand" || op == "nor";
            if (op != previousOp || nonAssoc) {
                error("logical operators cannot be mixed or chained without parentheses "
                      "(IEEE 1076-2008 9.1)");
                return nullptr;
            }
        }
        previousOp = op;
        ExprP node = std::make_unique<Expr>(Expr::BINARY);
        node->flp = m_cur.flp;
        node->text = op;
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
    if (isSym("+") || isSym("-") || isSym("??") || isId("abs") || isId("not") || isId("and")
        || isId("or") || isId("xor") || isId("nand") || isId("nor") || isId("xnor")) {
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
    if (m_cur.kind == Token::SYM && m_cur.text == "<<") {  // IEEE 1800-2023 ch.13 external name
        unsupported("external name");
        return nullptr;
    }
    if (m_cur.kind == Token::INT || m_cur.kind == Token::REAL || m_cur.kind == Token::BITS
        || m_cur.kind == Token::CHAR) {
        ExprP node;
        if (m_cur.kind == Token::INT) {
            node = std::make_unique<Expr>(Expr::INT_LIT);
            node->intValue = m_cur.intValue;
        } else if (m_cur.kind == Token::REAL) {
            node = std::make_unique<Expr>(Expr::REAL_LIT);
            node->realValue = m_cur.realValue;
        } else if (m_cur.kind == Token::BITS) {
            node = std::make_unique<Expr>(Expr::BITS_LIT);
            node->bits = m_cur.bits;
        } else {
            node = std::make_unique<Expr>(Expr::CHAR_LIT);
            node->charValue = m_cur.charValue;
        }
        node->flp = m_cur.flp;
        advance();
        if ((node->kind == Expr::INT_LIT || node->kind == Expr::REAL_LIT)
            && m_cur.kind == Token::ID && !isReservedWord(m_cur.text)) {
            // A VHDL physical literal: <abstract literal> <unit_name>, IEEE 1076-2008 9.4.
            // Which physical type the unit belongs to is not known until the literal reaches
            // its use, so the abstract literal is kept unscaled and takes the unit's factor
            // during lowering.
            node->text = m_cur.text;
            node->spelling = m_cur.spelling;
            if (node->kind == Expr::INT_LIT) node->realValue = static_cast<double>(node->intValue);
            node->kind = Expr::PHYS_LIT;
            advance();  // The unit name
        }
        return node;
    }
    if (m_cur.kind == Token::STRING) {
        // A string of logic characters is a bit-vector value, e.g. "1001" or
        // "10--", but a report message is a string whatever its characters are
        std::string bits;
        bool allBits = m_stringContext == 0;
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
            node->text = m_cur.text;
        }
        advance();
        // A string literal followed by an actual part is an operator-symbol call,
        // e.g. `"or"(a)` for the overloaded `or` operator
        if (!allBits && isSym("(")) {
            ExprP call = std::make_unique<Expr>(Expr::CALL);
            call->flp = node->flp;
            call->text = node->text;
            call->spelling = node->spelling;
            advance();
            for (;;) {
                ExprP arg = parseExpr();
                if (!arg) return nullptr;
                call->args.push_back(std::move(arg));
                if (!acceptSym(",")) break;
            }
            expectSym(")");
            return call;
        }
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
            ExprP item = parseChoice();
            if (!item) return nullptr;
            if (agg->kind == Expr::AGG_NAMED || isSym("=>") || isSym("|")) {
                // Named association: one or more choices, then the associated value
                agg->kind = Expr::AGG_NAMED;
                std::vector<ExprP> choiceList;
                choiceList.push_back(std::move(item));
                while (acceptSym("|")) {
                    ExprP more = parseChoice();
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
                // Type qualification: `type_mark'(expression)`; consume only the
                // parenthesized operand, so a following operator is not absorbed
                ExprP operand = parsePrimary();
                if (!operand) return nullptr;
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
        if (acceptSym(")")) {  // An empty actual list is always a subprogram call
            ExprP call = std::make_unique<Expr>(Expr::CALL);
            call->flp = node->flp;
            call->text = node->text;
            if (node->kind == Expr::NAME) {
                call->spelling = node->spelling;
            } else {
                call->a = std::move(node);  // Qualified name: the prefix chain
            }
            node = std::move(call);
            continue;
        }
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
            std::vector<ExprP> actuals;
            std::vector<std::string> formalNames;
            bool named = false;
            for (;;) {
                if (acceptSym("=>")) {
                    if (first->kind != Expr::NAME) {
                        unsupported("actual name in a subprogram call");
                        return nullptr;
                    }
                    named = true;
                    formalNames.push_back(first->text);
                    first = parseExpr();
                    if (!first) return nullptr;
                } else {
                    formalNames.emplace_back();
                }
                actuals.push_back(std::move(first));
                if (!acceptSym(",")) break;
                first = parseExpr();
                if (!first) return nullptr;
            }
            expectSym(")");
            if (named) {
                ExprP call = std::make_unique<Expr>(Expr::CALL);
                call->flp = node->flp;
                call->text = node->text;
                if (node->kind != Expr::NAME) call->a = std::move(node);
                call->args = std::move(actuals);
                call->formalNames = std::move(formalNames);
                node = std::move(call);
            } else {
                ExprP node2 = std::make_unique<Expr>(Expr::INDEX);
                node2->flp = node->flp;
                node2->a = std::move(node);
                node2->args = std::move(actuals);
                node = std::move(node2);
            }
        }
    }
    return node;
}

//! Consume `severity <note|warning|error|failure>`; only failure ends the run
Stmt::Severity Parser::parseSeverityValue() {
    if (!acceptId("severity")) return Stmt::SEV_NOTE;
    Token severityTok;
    if (!takeId(severityTok)) return Stmt::SEV_NOTE;
    if (severityTok.text == "warning") return Stmt::SEV_WARNING;
    if (severityTok.text == "error") return Stmt::SEV_ERROR;
    if (severityTok.text == "failure") return Stmt::SEV_FAILURE;
    return Stmt::SEV_NOTE;
}

//! Consume the severity of a report or an assert, if it has one
void Parser::parseSeverity(Stmt& stmt) { stmt.severity = parseSeverityValue(); }

bool Parser::parseRange(TypeSpec& type) {
    if (!type.flp) type.flp = m_cur.flp;  // An index constraint has no mark to take one from
    ExprP left = parseExpr();
    if (!left) return false;
    if (isId("downto")) {
        type.ascending = false;
        advance();
    } else if (isId("to")) {
        type.ascending = true;
        advance();
    } else if (left->kind == Expr::ATTR
               && (left->text == "range" || left->text == "reverse_range")) {
        // A discrete range may be a range attribute of a prefix, e.g. `integer range
        // mem_t'range`; it has no 'to'/'downto' and no right operand
        type.left = std::move(left);
        type.hasRange = true;
        return true;
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

//! The `name(range)` list of a record constraint, e.g. `rec_t(f(3 downto 0), g(x'range))`.
//! A member's range is either written out or named by a range attribute.
bool Parser::parseMemberConstraints(TypeSpec& type) {
    if (!type.flp) type.flp = m_cur.flp;
    for (;;) {
        if (m_failed) return false;
        TypeSpec::MemberConstraint member;
        Token memberTok;
        if (!takeId(memberTok)) return false;
        if (!type.flp) type.flp = memberTok.flp;
        member.name = memberTok.text;
        expectSym("(");
        member.lo = parseExpr();
        if (!member.lo) return false;
        if (isId("to")) {
            member.ascending = true;
            advance();
        } else if (isId("downto")) {
            advance();
        } else if (member.lo->kind == Expr::ATTR) {
            // The member takes the range of another object, `rec_t(f(sig'range))`
            member.range = std::move(member.lo);
            member.lo = nullptr;
            expectSym(")");
            type.memberConstraints.push_back(std::move(member));
            if (acceptSym(",")) continue;
            return true;
        } else {
            error("expected 'to' or 'downto' in a range, got '" + m_cur.text + "'");
            return false;
        }
        member.hi = parseExpr();
        if (!member.hi) return false;
        expectSym(")");
        type.memberConstraints.push_back(std::move(member));
        if (acceptSym(",")) continue;
        return true;
    }
}

void Parser::parseTypeSpec(TypeSpec& type) {
    Token markTok;
    if (!takeId(markTok)) return;
    type.flp = markTok.flp;
    // A library- or package-qualified mark uses only its final name; remember the
    // package qualifier (ignoring a leading library) so a type such as `work.defs.rec`
    // can be imported on demand when no `use work.defs.all` clause made it visible
    std::string pkg;
    while (isSym(".")) {
        advance();
        Token next;
        if (!takeId(next)) return;
        if (markTok.text != "work") pkg = markTok.text;
        markTok = next;
    }
    type.mark = markTok.text;
    type.pkg = pkg;
    // `name'subtype` takes the object's own subtype, `name'element` the element type of
    // an array; which of the two the name is, is only known when it is resolved
    if (isSym("'")) {
        const auto saved = m_lexer.state();
        const Token ahead = m_lexer.next();
        m_lexer.restore(saved);
        if (ahead.kind == Token::ID && (ahead.text == "subtype" || ahead.text == "element")) {
            advance();  // The apostrophe
            advance();  // The attribute name
            type.objectName = markTok.text;
            type.elementMark = ahead.text == "element";
            type.mark.clear();
            return;
        }
    }
    if (isId("range")) {
        advance();
        parseRange(type);
    } else if (isSym("(")) {
        advance();
        bool markOnly = false;  // `t(open)`: every index range stays unconstrained
        if (m_cur.kind == Token::ID) {
            const auto saved = m_lexer.state();
            const Token ahead = m_lexer.next();
            m_lexer.restore(saved);
            if (ahead.kind == Token::SYM && ahead.text == ")") {
                Token markTok2;
                if (!takeId(markTok2)) return;
                type.constraintMark = markTok2.text;
                expectSym(")");
                markOnly = true;  // A chained group may still constrain an element
            }
            // A record constraint's items are `name(range)`. A range constraint always carries a
            // 'to'/'downto' at the outer parentheses' own level; a record constraint does not.
            if (!markOnly && ahead.kind == Token::SYM && ahead.text == "(") {
                bool sawTopRange = false;
                int depth = 1;  // Inside the outer '('; m_cur is the first item's name
                for (;;) {
                    const Token t = m_lexer.next();
                    if (t.kind == Token::END) break;
                    if (t.kind == Token::SYM && t.text == "(") {
                        ++depth;
                    } else if (t.kind == Token::SYM && t.text == ")") {
                        if (--depth == 0) break;
                    } else if (depth == 1 && t.kind == Token::ID
                               && (t.text == "to" || t.text == "downto")) {
                        sawTopRange = true;
                    }
                }
                m_lexer.restore(saved);
                if (!sawTopRange) {
                    if (!parseMemberConstraints(type)) return;
                    expectSym(")");
                    return;
                }
            }
        }
        if (!markOnly) {
            if (!parseRange(type)) return;
            // `t(r1, r2)` constrains the same nested array as the chained `t(r1)(r2)`
            while (acceptSym(",")) {
                type.indexConstraints.emplace_back();
                if (!parseRange(type.indexConstraints.back())) return;
            }
            expectSym(")");
        }
    }
    while (isSym("(")) {  // A further index range of a nested `t(r1)(r2)` subtype
        // A `name(range)` list is a record constraint on this level, not an index range, and
        // it may hold a single member: `t(open)(f(r))`. An attribute at this level, as in
        // `t(v'range)(v(v'low)'range)`, marks the range form just as 'to'/'downto' does.
        const auto saved = m_lexer.state();
        bool sawTopRange = false;
        bool sawTopAttribute = false;
        bool sawTopComma = false;
        bool sawMemberRange = false;  // A range inside the first member's own parentheses
        bool tick = false;  // The previous token was an apostrophe at that depth
        for (int depth = 1;;) {  // m_cur is the '(', so the scan starts inside it
            const Token t = m_lexer.next();
            if (t.kind == Token::END) break;
            if (t.kind == Token::SYM && t.text == "(") {
                ++depth;
            } else if (t.kind == Token::SYM && t.text == ")") {
                if (--depth == 0) break;
            } else if (depth == 1 && t.kind == Token::ID
                       && (t.text == "to" || t.text == "downto")) {
                sawTopRange = true;
            } else if (depth == 1 && t.kind == Token::SYM && t.text == "'") {
                sawTopAttribute = true;
            } else if (depth == 1 && t.kind == Token::SYM && t.text == ",") {
                sawTopComma = true;
            } else if (depth == 2 && t.kind == Token::ID
                       && (t.text == "to" || t.text == "downto")) {
                sawMemberRange = true;
            } else if (depth == 2 && tick && t.kind == Token::ID
                       && (t.text == "range" || t.text == "reverse_range")) {
                sawMemberRange = true;
            }
            tick = depth == 2 && t.kind == Token::SYM && t.text == "'";
        }
        m_lexer.restore(saved);
        if (sawTopComma || sawMemberRange) {  // A record constraint on this level
            advance();  // The '('
            type.indexConstraints.emplace_back();
            type.indexConstraints.back().mark = type.mark;  // For the message if a name is wrong
            if (!parseMemberConstraints(type.indexConstraints.back())) return;
            expectSym(")");
            continue;
        }
        if (!sawTopRange && !sawTopAttribute) {
            unsupported("multi-dimensional array type");
            return;
        }
        advance();  // The '('
        type.indexConstraints.emplace_back();
        if (!parseRange(type.indexConstraints.back())) return;
        expectSym(")");
    }
}

void Parser::parseNameList(std::vector<std::pair<std::string, std::string>>& names) {
    for (;;) {
        Token nameTok;
        if (!takeName(nameTok)) return;
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
        (void)acceptId("signal");  // IEEE 1800-2023 17.2 interface-object keyword
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
        if (acceptSym(":=")) {
            port.init = parseExpr();
            if (!port.init) return;
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
void Parser::parseGenericClauseInto(std::vector<GenericDecl>& generics) {
    if (!acceptSym("(")) {
        error("expected '(' after 'generic'");
        return;
    }
    for (;;) {
        if (m_failed) return;
        if (isId("type")) {
            advance();
            GenericDecl generic;
            generic.flp = m_cur.flp;
            generic.isType = true;
            Token typeName;
            if (!takeId(typeName)) return;
            addName(generic.names, typeName);
            generics.push_back(std::move(generic));
            if (acceptSym(";")) continue;
            if (acceptSym(")")) break;
            error("expected ';' or ')' in a generic clause, got '" + m_cur.text + "'");
            return;
        }
        if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
            // A generic subprogram parameter: parse the signature, but only the name is kept
            GenericDecl generic;
            generic.flp = m_cur.flp;
            generic.isSubprogram = true;
            SubprogramDecl spec{SubprogramDecl::FUNCTION};
            if (!parseSubprogramSpec(spec)) return;
            generic.names.emplace_back(spec.name, spec.spelling);
            generics.push_back(std::move(generic));
            if (acceptSym(";")) continue;
            if (acceptSym(")")) break;
            error("expected ';' or ')' in a generic clause, got '" + m_cur.text + "'");
            return;
        }
        GenericDecl generic;
        generic.flp = m_cur.flp;
        (void)acceptId("constant");  // IEEE 1800-2023 17.2 interface-object keyword
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
        generics.push_back(std::move(generic));
        if (acceptSym(";")) continue;
        if (acceptSym(")")) break;
        error("expected ';' or ')' in a generic clause, got '" + m_cur.text + "'");
        return;
    }
    expectSym(";");
}

void Parser::parseEntity(FileLine* flp) {
    Token nameTok;
    if (!takeName(nameTok)) return;
    EntityUnit entity;
    entity.flp = flp;
    entity.name = nameTok.text;
    entity.spelling = nameTok.spelling;
    entity.libname = m_libname;
    entity.inLibrary = m_inLibrary;
    entity.uses = m_uses;
    entity.contextNames = m_contextNames;
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
            parseGenericClauseInto(entity.generics);
            continue;
        }
        if (acceptId("begin")) {
            // The statement part holds concurrent assertions and passive processes
            parseConcurrentList(entity.stmts);
            if (m_failed) return;
            break;
        }
        if (isId("end")) break;
        if (isId("package")) {
            // `package NAME is new BASE ...;` in the entity declarative part
            advance();
            Token instTok;
            if (!takeId(instTok)) return;
            if (!expectId("is")) return;
            parsePackageInstanceTail(instTok.flp, instTok.text, instTok.spelling);
            continue;
        }
        if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
            parseSubprogramInto(entity.subprograms);
            continue;
        }
        if (isId("use")) {
            parseUseClauseInto(entity.uses);
            continue;
        }
        if (isId("context")) {
            parseContextClause(entity.contextNames, false);
            continue;
        }
        if (isId("view")) {  // IEEE 1800-2023 ch.17 interface view
            unsupported("interface view");
            return;
        }
        if (isId("attribute") || isId("default") || isId("disconnect")) {
            // Attribute specifications and PSL directives have no synthesis meaning
            skipContextClause();
            continue;
        }
        if (isSym("{")) {  // A PSL directive block
            skipToBraceBlock();
            continue;
        }
        if (isId("component")) {
            advance();
            parseComponentDecl();
            continue;
        }
        if (skipFileDecl()) continue;
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

void Parser::parseFileDeclInto(std::vector<Decl>& decls) {
    Decl decl{Decl::FILE};
    decl.flp = m_cur.flp;
    parseNameList(decl.names);
    if (m_failed) return;
    parseTypeSpec(decl.type);
    if (m_failed) return;
    if (acceptId("open")) {
        Token mode;
        if (!takeId(mode)) return;
        decl.fileMode = mode.spelling;
        if (m_failed) return;
    }
    // `is <logical name>` is optional: a file can be declared without one
    if (acceptId("is")) {
        decl.init = parseExpr();
        if (!decl.init) return;
    }
    if (!acceptSym(";")) {
        error("expected ';' after a file declaration");
        return;
    }
    decls.push_back(std::move(decl));
}

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

//! Parse `( name_list [mode] : subtype_indication [:= default] ; ... )` of a subprogram
void Parser::parseInterfaceList(std::vector<FormalDecl>& formals) {
    if (!acceptSym("(")) return;
    for (;;) {
        if (m_failed) return;
        if (acceptSym(")")) break;
        FormalDecl formal;
        formal.flp = m_cur.flp;
        if (acceptId("constant")) {
            formal.cls = FormalDecl::CONSTANT;
        } else if (acceptId("signal")) {
            formal.cls = FormalDecl::SIGNAL;
        } else if (acceptId("variable")) {
            formal.cls = FormalDecl::VARIABLE;
        }
        parseNameList(formal.names);
        if (m_failed) return;
        // IEEE 1800-2023 4.3.3: an omitted mode means `in`, `buffer` is a readable `out`
        if (acceptId("in")) {
            formal.mode = PortMode::IN;
        } else if (acceptId("out")) {
            formal.mode = PortMode::OUT;
        } else if (acceptId("inout")) {
            formal.mode = PortMode::INOUT;
        } else if (acceptId("buffer")) {
            formal.mode = PortMode::OUT;
        } else if (isId("linkage")) {
            unsupported("subprogram parameter mode 'linkage'");
            return;
        }
        parseTypeSpec(formal.type);
        if (m_failed) return;
        if (acceptSym(":=")) {
            formal.init = parseExpr();
            if (!formal.init) return;
        }
        formals.push_back(std::move(formal));
        if (acceptSym(";")) continue;
        if (acceptSym(")")) break;
        error("expected ';' or ')' in an interface list, got '" + m_cur.text + "'");
        return;
    }
}

//! Parse a subprogram specification (no body): `[pure|impure] kind name [formals] [return T]`
bool Parser::parseSubprogramSpec(SubprogramDecl& sub) {
    bool impure = false;
    if (acceptId("pure")) {
    } else if (acceptId("impure")) {
        impure = true;
    }
    if (!isId("function") && !isId("procedure")) {
        error("expected 'function' or 'procedure', got '" + m_cur.text + "'");
        return false;
    }
    sub.kind = isId("function") ? SubprogramDecl::FUNCTION : SubprogramDecl::PROCEDURE;
    if (impure && sub.kind == SubprogramDecl::PROCEDURE) {
        error("'impure' applies only to a function");
        return false;
    }
    sub.impure = impure;
    advance();  // 'function' / 'procedure'
    Token nameTok;
    if (!takeDesignator(nameTok)) return false;
    sub.flp = nameTok.flp;
    sub.name = nameTok.text;
    sub.spelling = nameTok.spelling;
    parseInterfaceList(sub.formals);
    if (m_failed) return false;
    if (sub.kind == SubprogramDecl::FUNCTION) {
        if (!expectId("return")) return false;
        parseTypeSpec(sub.returnType);
        if (m_failed) return false;
    }
    return true;
}

//! Parse a subprogram declaration: a package prototype, or a full declaration with a body
void Parser::parseSubprogramInto(std::vector<SubprogramDecl>& out) {
    SubprogramDecl sub{SubprogramDecl::FUNCTION};
    if (!parseSubprogramSpec(sub)) return;
    if (acceptSym(";")) {  // A prototype; the body may arrive with the package body
        out.push_back(std::move(sub));
        return;
    }
    if (!expectId("is")) return;
    while (!m_failed && !isId("begin")) {
        if (m_cur.kind == Token::END) {
            error("unexpected end of file in a subprogram declarative part");
            return;
        }
        if (isId("use")) {
            skipContextClause();
            continue;
        }
        if (isId("variable")) {
            advance();
            parseDeclInto(sub.decls, Decl::VARIABLE);
            continue;
        }
        if (isId("constant")) {
            advance();
            parseDeclInto(sub.decls, Decl::CONSTANT);
            continue;
        }
        if (isId("type")) {
            advance();
            parseTypeDeclInto(sub.types);
            continue;
        }
        if (isId("subtype")) {
            advance();
            parseSubtypeInto(sub.types);
            continue;
        }
        if (isId("alias")) {
            advance();
            parseAliasInto(sub.aliases);
            continue;
        }
        if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
            parseSubprogramInto(sub.subprograms);
            continue;
        }
        if (isId("file")) {
            // The one declaration to capture rather than skip: a file image is read
            // at elaboration by the subprogram that declares it
            advance();
            parseFileDeclInto(sub.decls);
            continue;
        }
        unsupported("subprogram declarative item '" + m_cur.text + "'");
        return;
    }
    if (m_failed) return;
    advance();  // 'begin'
    sub.body = parseStmtList(false);
    if (m_failed) return;
    sub.hasBody = true;
    if (!expectId("end")) return;
    if (sub.kind == SubprogramDecl::FUNCTION) {
        (void)acceptId("function");
    } else {
        (void)acceptId("procedure");
    }
    if (m_cur.kind == Token::ID || m_cur.kind == Token::STRING) {
        Token endTok;
        if (!takeDesignator(endTok)) return;
        if (endTok.text != sub.name) {
            error(sub.flp, "subprogram end name '" + endTok.spelling + "' does not match '"
                               + sub.spelling + "'");
            return;
        }
    }
    if (!acceptSym(";")) {
        error("expected ';' after 'end' of subprogram");
        return;
    }
    out.push_back(std::move(sub));
}

//! Parse the index constraint of an array type definition
void Parser::parseIndexConstraint(TypeSpec& type) {
    if (m_cur.kind == Token::ID && !isId("to") && !isId("downto")) {
        const Token savedTok = m_cur;
        const auto savedState = m_lexer.state();
        Token markTok;
        if (!takeId(markTok)) return;
        if (isSym("-") || isSym("+") || isSym("*") || isId("to") || isId("downto")) {
            // The identifier began a range expression, e.g. `size - 1 downto 0`
            m_cur = savedTok;
            m_lexer.restore(savedState);
            parseRange(type);
            return;
        }
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
    if (!takeName(nameTok)) return;
    TypeDecl decl{TypeDecl::ARRAY};
    decl.flp = nameTok.flp;
    decl.name = nameTok.text;
    decl.spelling = nameTok.spelling;
    if (!expectId("is")) return;
    if (isSym("(")) {  // Enumeration
        decl.kind = TypeDecl::ENUM;
        advance();
        for (;;) {
            if (m_cur.kind == Token::CHAR) {
                // A character enumeration literal such as 'z'; store a name that the
                // lowering reproduces from the character value.
                const std::string lit = std::string{"'"} + m_cur.charValue + "'";
                decl.literals.emplace_back(lit, lit);
                advance();
            } else {
                Token literalTok;
                if (!takeId(literalTok)) return;
                addName(decl.literals, literalTok);
            }
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
        while (acceptSym(",")) {
            decl.indexes.emplace_back();
            parseIndexConstraint(decl.indexes.back());
            if (m_failed) return;
        }
        expectSym(")");
        if (!expectId("of")) return;
        parseTypeSpec(decl.base);
        if (m_failed) return;
    } else if (isId("range")) {  // A physical type definition, IEEE 1076-2008 3.2.2.1
        advance();
        if (!parseRange(decl.base)) return;
        if (!isId("units")) {
            // An integer type definition starts the same way and is not modelled
            unsupported("type definition 'range'");
            return;
        }
        decl.kind = TypeDecl::PHYSICAL;
        advance();
        for (;;) {
            if (m_failed) return;
            if (isId("end")) break;
            TypeDecl::Unit unit;
            Token unitTok;
            if (!takeId(unitTok)) return;
            unit.name = unitTok.text;
            unit.spelling = unitTok.spelling;
            if (acceptSym("=")) {
                unit.value = parsePrimary();
                if (m_failed) return;
            }
            if (!acceptSym(";")) {
                error("expected ';' after a unit declaration");
                return;
            }
            decl.units.push_back(std::move(unit));
        }
        if (!expectId("end")) return;
        if (!expectId("units")) return;
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
    if (!takeName(nameTok)) return;
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

//! Parse `alias name [: mark] is designator [ signature ];`
void Parser::parseAliasInto(std::vector<AliasDecl>& aliases) {
    Token nameTok;
    if (!takeName(nameTok)) return;
    AliasDecl alias;
    alias.flp = nameTok.flp;
    alias.name = nameTok.text;
    alias.spelling = nameTok.spelling;
    if (acceptSym(":")) parseTypeSpec(alias.mark);  // Mark with its index constraint
    if (!expectId("is")) return;
    alias.designator = parseDesignator();
    if (!alias.designator) return;
    if (acceptSym("[")) {  // Signature naming one of the designator's overloads
        alias.hasSignature = true;
        int depth = 1;
        while (depth > 0 && m_cur.kind != Token::END) {
            if (isSym("[")) {
                ++depth;
            } else if (isSym("]")) {
                --depth;
            }
            advance();
        }
    }
    if (!acceptSym(";")) {
        error("expected ';' after an alias declaration");
        return;
    }
    aliases.push_back(std::move(alias));
}

//! `component name [is] [generic (...);] [port (...);] end component [name];`
//! The declaration is dropped: a named instantiation binds to the entity of that name, and
//! the interface list only shapes the association check this frontend does not model.
void Parser::parseComponentDecl() {
    Token nameTok;
    if (!takeName(nameTok)) return;
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
    if (acceptSym("(")) {  // Architecture selection: `entity work.X(ARCH)`; only one is kept
        if (isSym("-")) {
            advance();  // `-` selects any architecture
        } else {
            Token archTok;
            if (!takeId(archTok)) return;
        }
        expectSym(")");
    }
    inst->entityInstantiation = direct;
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

//! `new [work.]BASE [generic map (...)];` following `package NAME is`
void Parser::parsePackageInstanceTail(FileLine* flp, const std::string& name,
                                      const std::string& spelling) {
    if (!expectId("new")) return;
    PackageInstance inst;
    inst.flp = flp;
    inst.name = name;
    inst.spelling = spelling;
    inst.libname = m_libname;
    inst.inLibrary = m_inLibrary;
    inst.uses = m_uses;
    inst.contextNames = m_contextNames;
    if (isId("work")) {
        advance();
        if (!acceptSym(".")) {
            error("expected '.' after 'work'");
            return;
        }
    }
    Token baseTok;
    if (!takeId(baseTok)) return;
    inst.baseName = baseTok.text;
    if (isId("generic")) {
        advance();
        if (!expectId("map")) return;
        parseAssocList(inst.genericAssociations, "generic");
        if (m_failed) return;
    }
    if (!acceptSym(";")) {
        error("expected ';' after a package instantiation");
        return;
    }
    m_packageInstances.push_back(std::move(inst));
}

//! `(formal [range] => actual, ...)` associations of a port or generic map
void Parser::parseAssocList(std::vector<PortAssoc>& out, const char* what) {
    if (!acceptSym("(")) {
        error(std::string{"expected '(' after '"} + what + " map'");
        return;
    }
    int mixMode = 0;  // 1: the list is positional; 2: it is named
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
                while (isSym(".")) {  // A record-member formal `rec.field`
                    advance();
                    if (m_cur.kind == Token::ID) advance();
                }
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
            // IEEE 1076-2008 6.5.7.2: the associations are all positional or all named
            if (mixMode == 1) {
                error(std::string{"named association after a positional one in a "} + what
                      + " map (IEEE 1076-2008 6.5.7.2)");
                return;
            }
            mixMode = 2;
            Token formalTok;
            if (!takeId(formalTok)) return;
            assoc.formal = formalTok.text;
            assoc.formalSpelling = formalTok.spelling;
            if (isSym(".")) {  // A formal naming one element of a record port
                advance();
                Token memberTok;
                if (!takeId(memberTok)) return;
                assoc.formalMember = memberTok.text;
                assoc.formalMemberSpelling = memberTok.spelling;
                if (isSym(".")) {  // A nested record element is not modelled
                    unsupported("nested record-member port association");
                    return;
                }
            }
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
        } else if (isId("open")) {
            // A positional association may leave the formal open (IEEE 1076-2008
            // 6.5.7.1), which the lowering resolves to the formal's default
            if (mixMode == 2) {
                error(std::string{"positional association after a named one in a "} + what
                      + " map (IEEE 1076-2008 6.5.7.2)");
                return;
            }
            mixMode = 1;
            advance();
        } else {
            // Positional association: the actual maps to the next item in order
            if (mixMode == 2) {
                error(std::string{"positional association after a named one in a "} + what
                      + " map (IEEE 1076-2008 6.5.7.2)");
                return;
            }
            mixMode = 1;
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
    if (!takeName(nameTok)) return;
    Architecture arch;
    arch.flp = flp;
    arch.name = nameTok.text;
    arch.libname = m_libname;
    arch.inLibrary = m_inLibrary;
    arch.uses = m_uses;
    arch.contextNames = m_contextNames;
    if (!expectId("of")) return;
    Token entityTok;
    if (!takeId(entityTok)) return;
    arch.entityName = entityTok.text;
    if (!expectId("is")) return;
    for (;;) {
        if (m_failed) return;
        if (isId("begin")) break;
        if (isId("use")) {
            parseUseClauseInto(arch.uses);
            continue;
        }
        if (isId("context")) {
            parseContextClause(arch.contextNames, false);
            continue;
        }
        if (isId("view")) {  // IEEE 1800-2023 ch.17 interface view
            unsupported("interface view");
            return;
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
            parseComponentDecl();
            continue;
        }
        if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
            parseSubprogramInto(arch.subprograms);
            continue;
        }
        if (isId("package")) {
            // `package NAME is new BASE ...;` in the architecture declarative part
            advance();
            Token instTok;
            if (!takeId(instTok)) return;
            if (!expectId("is")) return;
            parsePackageInstanceTail(instTok.flp, instTok.text, instTok.spelling);
            continue;
        }
        if (isId("for")) {  // IEEE 1800-2023 19.4 configuration specification
            advance();
            skipToSemicolon();
            continue;
        }
        if (skipFileDecl()) continue;
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

//! One choice: an expression, optionally a discrete range such as `1 to 2`
ExprP Parser::parseChoice() {
    ExprP choice = parseExpr();
    if (!choice) return nullptr;
    if (!isId("to") && !isId("downto")) return choice;
    ExprP range = std::make_unique<Expr>(Expr::SLICE);
    range->flp = choice->flp;
    range->text = m_cur.text;
    advance();
    range->b = std::move(choice);
    range->c = parseExpr();
    if (!range->c) return nullptr;
    return range;
}

//! Parse a choice list, as in a case alternative, selected assignment or
//! aggregate association; an empty list means "others"
bool Parser::parseChoices(std::vector<ExprP>& choices) {
    if (isId("others")) {
        advance();
        return true;
    }
    for (;;) {
        ExprP choice = parseChoice();
        if (!choice) return false;
        choices.push_back(std::move(choice));
        if (!acceptSym("|")) return true;
    }
}

void Parser::parseConcurrentStatement(Architecture& arch) {
    if (m_failed) return;
    FileLine* const flp = m_cur.flp;
    if (isId("default")) {  // A PSL directive has no synthesis meaning
        skipContextClause();
        return;
    }
    if (isSym("{")) {  // A PSL directive block
        skipToBraceBlock();
        return;
    }
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
    if (isId("cover") || isId("restrict") || isId("assume") || isId("fair")
        || isId("fairness")) {  // A PSL verification directive (IEEE 1800.1 ch.5)
        const auto pslSaved = m_lexer.state();
        const Token pslAhead = m_lexer.next();
        m_lexer.restore(pslSaved);
        if (pslAhead.kind == Token::SYM && pslAhead.text == "{") {
            advance();  // The directive keyword
            skipToStatementEnd();  // The property through its terminating ';'
            return;
        }
    }
    if (isId("entity") || isId("component")) {
        // `entity NAME` and the `component NAME` spelling of an instantiation both name the
        // entity to instantiate
        advance();
        parseInstance(arch, flp, label, true);
        return;
    }
    if (m_cur.kind == Token::ID) {
        // `label : child [generic map] port map (...)`: a component or entity instantiation.
        // The entity may be selected: `work.pkg.child`
        const auto saved = m_lexer.state();
        const Token nameTok = m_cur;
        advance();
        while (isSym(".")) {
            advance();
            if (m_cur.kind != Token::ID) break;
            advance();
        }
        // An `entity` head names an instantiation even with no map at all
        const bool mapped = nameTok.text == "entity"
                            || ((isId("port") || isId("generic")) && m_cur.text != "process");
        m_cur = nameTok;
        m_lexer.restore(saved);
        if (mapped) {
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
        if (!takeName(varTok)) return;
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
        parseConcurrentList(gen->body, &gen->decls, &gen->types, &gen->subprograms);
        if (m_failed) return;
        if (!parseGenerateEnd()) return;
        arch.stmts.push_back(std::move(gen));
        return;
    }
    if (isId("if")) {
        advance();
        auto gen = std::make_unique<Concurrent>(Concurrent::GEN_IF);
        gen->flp = flp;
        for (;;) {
            GenBranch branch;
            // A condition may carry a label, `if name: condition generate`. The label is a
            // name and nothing else, and GHDL and nvc both take the expression after it as
            // the condition, so the label itself is dropped
            if (m_cur.kind == Token::ID) {
                const auto saved = m_lexer.state();
                const Token nameTok = m_cur;
                advance();
                const bool labelled = isSym(":");
                m_cur = nameTok;
                m_lexer.restore(saved);
                if (labelled) {
                    advance();  // The label's name
                    advance();  // The colon
                }
            }
            branch.cond = parseExpr();
            if (!branch.cond) return;
            if (!expectId("generate")) return;
            parseConcurrentList(branch.stmts, &branch.decls, &branch.types, &branch.subprograms);
            if (m_failed) return;
            gen->branches.push_back(std::move(branch));
            if (acceptId("elsif")) continue;
            if (acceptId("else")) {
                GenBranch elseBranch;  // The else part carries no condition
                if (!expectId("generate")) return;
                parseConcurrentList(elseBranch.stmts, &elseBranch.decls, &elseBranch.types,
                                    &elseBranch.subprograms);
                if (m_failed) return;
                gen->branches.push_back(std::move(elseBranch));
            }
            break;
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
            if (isId("attribute") || isId("disconnect")) {
                // Attribute and disconnection specifications have no synthesis meaning
                skipContextClause();
                continue;
            }
            if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
                parseSubprogramInto(block->subprograms);
                continue;
            }
            if (isId("component")) {
                advance();
                parseComponentDecl();
                continue;
            }
            if (skipFileDecl()) continue;
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
        if (looksLikePslAssertion()) {  // A PSL assertion has no synthesis meaning
            advance();
            skipToStatementEnd();
            return;
        }
        advance();
        auto conc = std::make_unique<Concurrent>(Concurrent::ASSERT);
        conc->flp = flp;
        conc->target = parseExpr();  // The condition
        if (!conc->target) return;
        if (acceptId("report")) {
            ++m_stringContext;
            conc->value = parseExpr();
            --m_stringContext;
            if (!conc->value) return;
        }
        conc->severity = parseSeverityValue();
        if (!acceptSym(";")) error("expected ';' after 'assert'");
        arch.stmts.push_back(std::move(conc));
        return;
    }
    if (isId("entity") || isId("component") || isId("case")) {
        unsupported("concurrent statement '" + m_cur.text + "'");
        return;
    }
    ExprP target;
    if (m_cur.kind == Token::ID) {
        target = parseDesignator();
    } else if (isSym("(")) {
        // An aggregate assignment target, e.g. `(a, b) <= value`. Parse only the
        // primary, so the following `<=` is not taken as a relational operator.
        target = parsePrimary();
    } else {
        error("expected a concurrent statement, got '" + m_cur.text + "'");
        return;
    }
    if (!target) return;
    if (acceptSym("<=")) {
        auto conc = std::make_unique<Concurrent>(Concurrent::ASSIGN);
        conc->flp = flp;
        conc->target = std::move(target);
        if (isId("transport") || isId("inertial")) {
            unsupported("delayed signal assignment ('" + m_cur.text + "')");
            return;
        }
        conc->value = isId("unaffected") ? parseWaveformValue() : parseExpr();
        if (!conc->value) return;
        if (isId("when")) {
            conc->kind = Concurrent::COND_ASSIGN;
            conc->alternatives.emplace_back(std::move(conc->value), nullptr);
            while (acceptId("when")) {
                ExprP cond = parseExpr();
                if (!cond) return;
                conc->alternatives.back().second = std::move(cond);
                if (!acceptId("else")) break;
                ExprP value = isId("unaffected") ? parseWaveformValue() : parseExpr();
                if (!value) return;
                conc->alternatives.emplace_back(std::move(value), nullptr);
            }
        }
        if (isId("after")) {  // A delayed concurrent assignment drives the target in time
            advance();
            conc->delay = parseExpr();
            if (!conc->delay) return;
        }
        // A waveform list: further elements, each with its own delay (IEEE 1800-2023 10.6)
        if (conc->kind == Concurrent::ASSIGN) {
            while (acceptSym(",")) {
                ExprP value = parseExpr();
                if (!value) return;
                ExprP delay;
                if (acceptId("after")) {
                    delay = parseExpr();
                    if (!delay) return;
                }
                conc->waveform.emplace_back(std::move(value), std::move(delay));
            }
        }
        expectSym(";");
        arch.stmts.push_back(std::move(conc));
        return;
    }
    if (isSym(":=")) {
        unsupported("variable assignment at architecture level");
        return;
    }
    if (!acceptSym(";")) {
        error("expected ';' after a procedure call, got '" + m_cur.text + "'");
        return;
    }
    auto call = std::make_unique<Concurrent>(Concurrent::CALL);
    call->flp = flp;
    call->value = std::move(target);
    arch.stmts.push_back(std::move(call));
}

//! Parse concurrent statements up to `end [generate|block]`, without the terminator
void Parser::parseConcurrentList(std::vector<std::unique_ptr<Concurrent>>& out,
                                 std::vector<Decl>* decls, std::vector<TypeDecl>* types,
                                 std::vector<SubprogramDecl>* subprograms) {
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
        if (subprograms
            && (isId("function") || isId("procedure") || isId("pure") || isId("impure"))) {
            parseSubprogramInto(*subprograms);
            continue;
        }
        if (isId("attribute") || isId("default") || isId("disconnect")) {
            // An attribute specification, like a verification directive, has no synthesis
            // meaning; a vendor tool reads it from the source, the netlist does not change
            skipContextClause();
            continue;
        }
        if (isId("component")) {
            // Dropped: an instantiation naming this component binds the entity of the name
            advance();
            parseComponentDecl();
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
                if (m_failed) return;
                // A bare name is a sensitivity signal; any other item makes the process
                // combinational over every signal it reads (IEEE 1800-2023 11.1.3)
                const auto saved = m_lexer.state();
                const Token item = m_cur;
                const Token after = m_lexer.next();
                m_lexer.restore(saved);
                const bool simple = item.kind == Token::ID && after.kind == Token::SYM
                                    && (after.text == "," || after.text == ")");
                if (!simple) {
                    process.sensitivityAll = true;
                    process.sensitivity.clear();
                    int depth = 0;
                    while (m_cur.kind != Token::END) {
                        if (isSym(")")) {
                            if (depth == 0) break;
                            --depth;
                        } else if (isSym("(")) {
                            ++depth;
                        }
                        advance();
                    }
                    break;
                }
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
        if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
            parseSubprogramInto(process.subprograms);
            continue;
        }
        if (isId("alias")) {
            advance();
            parseAliasInto(process.aliases);
            continue;
        }
        if (skipFileDecl()) continue;
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
            advance();
            stmt->delay = parseExpr();
            if (!stmt->delay) return nullptr;
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
        ++m_stringContext;
        stmt->value = parseExpr();  // The message
        --m_stringContext;
        if (!stmt->value) return nullptr;
        parseSeverity(*stmt);
        expectSym(";");
        return stmt;
    }
    if (isId("assert")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::ASSERT);
        stmt->flp = flp;
        stmt->value = parseExpr();  // The condition
        if (!stmt->value) return nullptr;
        if (acceptId("report")) {
            ++m_stringContext;
            stmt->message = parseExpr();
            --m_stringContext;
            if (!stmt->message) return nullptr;
        }
        parseSeverity(*stmt);
        expectSym(";");
        return stmt;
    }
    if (isId("return")) {
        advance();
        auto stmt = std::make_unique<Stmt>(Stmt::RETURN);
        stmt->flp = flp;
        if (!isSym(";")) {
            stmt->value = parseExpr();
            if (!stmt->value) return nullptr;
        }
        expectSym(";");
        return stmt;
    }
    if (isId("null")) {
        advance();
        expectSym(";");
        return nullptr;
    }
    ExprP target;
    if (m_cur.kind == Token::ID) {
        target = parseDesignator();
    } else if (isSym("(")) {
        // An aggregate assignment target, e.g. `(a, b) := value`. Parse only the
        // primary, so the following `:=` is not mistaken for part of an expression.
        target = parsePrimary();
    } else {
        error("expected a sequential statement, got '" + m_cur.text + "'");
        return nullptr;
    }
    if (!target) return nullptr;
    if (acceptSym(":=")) {
        auto stmt = std::make_unique<Stmt>(Stmt::VAR_ASSIGN);
        stmt->flp = flp;
        stmt->target = std::move(target);
        stmt->value = parseExpr();
        if (!stmt->value) return nullptr;
        if (isId("when") && !parseConditionalTail(*stmt, false)) return nullptr;
        if (isId("after")) {
            unsupported("delayed variable assignment ('after')");
            return nullptr;
        }
        expectSym(";");
        return stmt;
    }
    // A designator that is not an assignment target is a procedure call
    if (!acceptSym("<=")) {
        if (!acceptSym(";")) {
            error("expected ':=', '<=' or ';' after a name, got '" + m_cur.text + "'");
            return nullptr;
        }
        auto stmt = std::make_unique<Stmt>(Stmt::CALL);
        stmt->flp = flp;
        stmt->value = std::move(target);
        return stmt;
    }
    auto stmt = std::make_unique<Stmt>(Stmt::ASSIGN);
    stmt->flp = flp;
    stmt->target = std::move(target);
    stmt->value = parseWaveformValue();
    if (!stmt->value) return nullptr;
    if (isId("when") && !parseConditionalTail(*stmt, true)) return nullptr;
    if (isId("after")) {  // A zero delay is a delta; anything else needs a timed update
        advance();
        stmt->delay = parseExpr();
        if (!stmt->delay) return nullptr;
    }
    expectSym(";");
    return stmt;
}

ExprP Parser::parseWaveformValue() {
    if (isId("unaffected")) {
        auto expr = std::make_unique<Expr>(Expr::UNAFFECTED);
        expr->flp = m_cur.flp;
        advance();
        return expr;
    }
    return parseExpr();
}

bool Parser::parseConditionalTail(Stmt& stmt, bool allowUnaffected) {
    // stmt.value holds the first waveform value and m_cur is the following 'when'.
    ExprP value = std::move(stmt.value);
    std::vector<std::pair<ExprP, ExprP>> alternatives;
    while (isId("when")) {
        advance();
        ExprP cond = parseExpr();
        if (!cond) return false;
        alternatives.emplace_back(std::move(value), std::move(cond));
        if (acceptId("else")) {
            value = allowUnaffected ? parseWaveformValue() : parseExpr();
            if (!value) return false;
            continue;
        }
        // With no trailing 'else', the target keeps its current value (`unaffected`)
        auto keep = std::make_unique<Expr>(Expr::UNAFFECTED);
        keep->flp = m_cur.flp;
        value = std::move(keep);
        break;
    }
    alternatives.emplace_back(std::move(value), nullptr);
    stmt.alternatives = std::move(alternatives);
    stmt.value = nullptr;
    return true;
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

//! Record `use lib.pkg[.item];`: `.all` imports the package, a name imports the
//! one declaration it names (IEEE 1076-2008 12.4)
void Parser::parseUseClause() { parseUseClauseInto(m_uses); }

void Parser::parseUseClauseInto(std::vector<UseClause>& uses) {
    advance();  // 'use'
    for (;;) {  // `use clause, clause;` -- IEEE 1076-2008 12.4
        std::vector<std::string> parts;
        for (;;) {
            if (m_cur.kind != Token::ID) break;
            parts.push_back(m_cur.text);
            advance();
            if (!acceptSym(".")) break;
        }
        // `use [lib.]pkg.all` or `use [lib.]pkg.item`; the library is not modelled
        if (parts.size() >= 2) {
            UseClause use;
            use.lib = parts.size() >= 3 ? parts[parts.size() - 3] : "";
            use.pkg = parts[parts.size() - 2];
            use.decl = parts.back() == "all" ? "" : parts.back();
            uses.push_back(std::move(use));
        }
        if (!acceptSym(",")) break;
    }
    expectSym(";");
}

//! Skip `library <name>;`, which only affects which library GHDL would search
void Parser::skipLibraryClause() {
    while (!m_failed && m_cur.kind != Token::END && !isSym(";")) advance();
    expectSym(";");
}

void Parser::parsePackage(FileLine* flp, bool isBody) {
    if (isBody && !expectId("body")) return;
    Token nameTok;
    if (!takeName(nameTok)) return;
    PackageUnit pkg;
    pkg.flp = flp;
    pkg.name = nameTok.text;
    pkg.spelling = nameTok.spelling;
    pkg.libname = m_libname;
    pkg.inLibrary = m_inLibrary;
    pkg.isBody = isBody;
    pkg.uses = m_uses;
    pkg.contextNames = m_contextNames;
    if (!expectId("is")) return;
    if (isId("new")) {  // `package X is new G ...;` generic-package instance
        parsePackageInstanceTail(flp, pkg.name, pkg.spelling);
        return;
    }
    for (;;) {
        if (m_failed) return;
        if (isId("end")) break;
        if (isId("use")) {
            parseUseClauseInto(pkg.uses);
            continue;
        }
        if (isId("context")) {
            parseContextClause(pkg.contextNames, false);
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
        if (isId("generic")) {  // A generic package declaration: a template
            advance();
            parseGenericClauseInto(pkg.generics);
            continue;
        }
        if (isId("function") || isId("procedure") || isId("pure") || isId("impure")) {
            parseSubprogramInto(pkg.subprograms);
            continue;
        }
        if (isId("view")) {  // IEEE 1800-2023 ch.17 interface view
            unsupported("interface view");
            return;
        }
        if (isId("attribute") || isId("disconnect") || isId("default")) {
            // Attribute specifications and PSL directives have no synthesis meaning
            skipContextClause();
            continue;
        }
        if (isSym("{")) {  // A PSL directive block
            skipToBraceBlock();
            continue;
        }
        if (isId("component")) {
            advance();
            parseComponentDecl();
            continue;
        }
        if (skipFileDecl()) continue;
        if (isId("signal") || isId("shared") || isId("variable") || isId("group")) {
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
    if (!takeName(varTok)) return nullptr;
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

//! `configuration name of entity is for arch (for insts : comp use entity lib.ent;) end for; end
//! configuration;`
void Parser::parseConfiguration(FileLine* flp) {
    ConfigurationUnit unit;
    unit.flp = flp;
    unit.libname = m_libname;
    unit.inLibrary = m_inLibrary;
    Token nameTok;
    if (!takeName(nameTok)) return;
    unit.name = nameTok.text;
    unit.spelling = nameTok.spelling;
    if (!expectId("of")) return;
    Token entityTok;
    if (!takeId(entityTok)) return;
    unit.entityName = entityTok.text;
    while (acceptSym(".")) {  // `work.t`: the library is not modelled
        Token partTok;
        if (!takeId(partTok)) return;
        unit.entityName = partTok.text;
    }
    if (!expectId("is")) return;
    if (!expectId("for")) return;
    Token archTok;
    if (!takeId(archTok)) return;
    unit.archName = archTok.text;
    for (;;) {
        if (m_failed) return;
        if (isId("end")) break;
        if (!isId("for")) {
            // A `use` clause of the configuration only makes declarations visible
            unsupported("configuration item '" + m_cur.text + "'");
            return;
        }
        advance();
        ConfigBinding binding;
        binding.flp = flp;
        for (;;) {  // The instantiation list, comma separated
            Token instTok;
            if (!takeId(instTok)) return;
            if (!binding.instances.empty()) binding.instances += ",";
            binding.instances += instTok.text;
            if (!acceptSym(",")) break;
        }
        expectSym(":");
        Token compTok;
        if (!takeId(compTok)) return;
        binding.component = compTok.text;
        if (!expectId("use")) return;
        if (!expectId("entity")) return;
        Token libTok;
        if (!takeId(libTok)) return;
        binding.libname = libTok.text;
        expectSym(".");
        Token entTok;
        if (!takeId(entTok)) return;
        binding.entity = entTok.text;
        while (acceptSym(".")) {  // An architecture selection is not modelled
            Token partTok;
            if (!takeId(partTok)) return;
            binding.entity = partTok.text;
        }
        expectSym(";");
        if (!expectId("end")) return;
        if (!expectId("for")) return;
        if (!acceptSym(";")) error("expected ';' after 'end for'");
        unit.bindings.push_back(std::move(binding));
    }
    // The architecture's own `end for`
    if (!expectId("end")) return;
    if (!expectId("for")) return;
    if (!acceptSym(";")) error("expected ';' after the architecture's 'end for'");
    if (!expectId("end")) return;
    if (!expectId("configuration")) return;
    if (m_cur.kind == Token::ID) advance();  // Optional end name
    if (!acceptSym(";")) error("expected ';' after 'end configuration'");
    if (!m_failed) m_configurations.push_back(std::move(unit));
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
            parseContextClause(m_contextNames, true);
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
        if (isId("default")) {  // A PSL directive
            skipContextClause();
            continue;
        }
        if (isId("vunit")) {  // A PSL verification unit: skip its header and body
            advance();
            skipToBraceBlock();
            continue;
        }
        if (isSym("{")) {  // A PSL directive block
            skipToBraceBlock();
            continue;
        }
        if (isId("configuration")) {  // IEEE 1800-2023 11.3 configuration design unit
            FileLine* const flp = m_cur.flp;
            advance();
            parseConfiguration(flp);
            continue;
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
    for (ConfigurationUnit& config : parser.configurations()) {
        library.configurations.push_back(std::move(config));
    }
    for (PackageInstance& inst : parser.packageInstances()) {
        library.packageInstances.push_back(std::move(inst));
    }
    for (ContextUnit& context : parser.contexts()) {
        library.contexts.push_back(std::move(context));
    }
}

}  // namespace vhdlnative
