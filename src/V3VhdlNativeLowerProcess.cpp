// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: lowering to the AST
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
// Lowers VHDL processes, sensitivity lists and clocked idioms
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3Error.h"
#include "V3FileLine.h"
#include "V3VhdlNativeLower.h"
#include "V3VhdlNativeLowerImpl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

void Lowering::lowerProcess(const Process& process) {
    m_edgeAsLevel = false;
    m_edgeAsPrev = false;
    m_edgePrevVars.clear();
    // Tracked variable values hold only within one process
    struct VarValsScope final {
        std::map<std::string, ExprP>& vals;
        explicit VarValsScope(std::map<std::string, ExprP>& vals_)
            : vals{vals_} {
            vals.clear();
        }
        ~VarValsScope() { vals.clear(); }
    } varValsScope{m_varVals};
    if (!process.hasSensitivity) {
        lowerWaitProcess(process);
        return;
    }
    // The process declarative part declares variables that hide module-level names
    const ScopeGuard scopeGuard{*this};
    std::set<std::string> varNames;
    emitProcessDecls(process, varNames);
    std::vector<AstVar*> sensVars;
    if (!resolveProcessSensitivity(process, varNames, sensVars)) return;
    // A process runs on every event of its sensitivity list, so an edge call must not
    // become a constant. A whole-signal edge is the signal's level when it is the only
    // sensitivity; a signal-element event and multi-sensitivity edges compare with the
    // previous value, which needs the previous-value variables maintained below.
    const bool hasEdges = stmtsContainEdge(process.body);
    const std::size_t sensCount = sensVars.size() + process.sensitivityElems.size();
    m_edgeAsLevel = hasEdges && sensCount == 1;
    m_edgeAsPrev = hasEdges && sensCount > 1;
    if (lowerFlopIdiom(process)) { return; }

    m_edges.clear();
    m_initial = false;
    m_varVals.clear();
    AstNodeStmt* bodyp = lowerStmtList(process.body);
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    // Record the current signal values for the next run's edge detection
    AstNodeStmt* initPrevsp = nullptr;
    for (const auto& entry : m_edgePrevVars) {
        AstNodeExpr* const lhsp = new AstVarRef{process.flp, entry.second, VAccess::WRITE};
        AstNodeExpr* const rhsp = new AstVarRef{process.flp, entry.first, VAccess::READ};
        initPrevsp = initPrevsp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(
                                      initPrevsp, new AstAssign{process.flp, lhsp, rhsp})
                                : new AstAssign{process.flp, lhsp, rhsp};
    }
    if (!m_edgePrevVars.empty() && bodyp && initPrevsp) {
        bodyp = AstNode::addNext(bodyp, initPrevsp->cloneTree(true));
    }
    m_initial = true;
    m_varVals.clear();
    AstNodeStmt* initp = lowerStmtList(process.body);
    m_initial = false;
    if (!m_edgePrevVars.empty() && initp && initPrevsp) {
        initp = AstNode::addNext(initp, initPrevsp);
        initPrevsp = nullptr;
    }
    if (initPrevsp) initPrevsp->deleteTree();

    bool bad = false;
    for (const Edge& edge : edges) {
        if (std::find(sensVars.begin(), sensVars.end(), edge.varp) == sensVars.end()) {
            unsupportedError(process.flp, "clock edge on a signal outside the sensitivity list");
            bad = true;
        }
    }
    if (bad) {
        if (bodyp) bodyp->deleteTree();
        if (initp) initp->deleteTree();
        return;
    }

    AstSenItem* const itemsp = buildProcessSensitivity(process, sensVars, edges, varNames);
    if (!itemsp) {
        if (bodyp) bodyp->deleteTree();
        if (initp) initp->deleteTree();
        return;
    }
    if (bodyp) {
        m_modp->addStmtsp(new AstAlways{process.flp, VAlwaysKwd::ALWAYS,
                                        new AstSenTree{process.flp, itemsp}, bodyp});
    } else if (itemsp) {
        itemsp->deleteTree();
    }
    // VHDL executes a process once at time zero, before any sensitivity event, and the
    // initial combination is visible to the testbench after the first eval
    if (initp) m_modp->addStmtsp(new AstInitialStatic{process.flp, initp});
}

bool Lowering::resolveProcessSensitivity(const Process& process,
                                         const std::set<std::string>& varNames,
                                         std::vector<AstVar*>& sensVars) {
    std::vector<std::string> sensNames = process.sensitivity;
    if (process.sensitivityAll) {
        // process(all) is sensitive to every signal it reads, including any clock, so
        // a clock edge in the body cannot become an edge-triggered always block
        if (stmtsContainEdge(process.body)) {
            unsupportedError(process.flp, "clock edge in a process(all)");
            return false;
        }
        std::set<std::string> readNames;
        collectReadNames(process.body, readNames);
        sensNames.assign(readNames.begin(), readNames.end());
    }
    for (const std::string& name : sensNames) {
        // A variable is not a signal, so it never becomes a sensitivity item
        if (varNames.count(name) || m_variables.count(name)) continue;
        Expr nameExpr{Expr::NAME};
        nameExpr.flp = process.flp;
        nameExpr.text = name;
        nameExpr.spelling = name;
        AstVar* const varp = lookupVar(nameExpr);
        if (!varp) {
            // process(all) collects read names including subprogram and type marks; only
            // names that are actual signals sensitize the process
            if (process.sensitivityAll) continue;
            process.flp->v3error("VHDL: unknown signal '" + name + "' in a sensitivity list.");
            return false;
        }
        sensVars.push_back(varp);
    }
    return true;
}

AstSenItem* Lowering::buildProcessSensitivity(const Process& process,
                                              const std::vector<AstVar*>& sensVars,
                                              const std::vector<Edge>& edges,
                                              const std::set<std::string>& varNames) {
    AstSenItem* itemsp = nullptr;
    for (AstVar* const varp : sensVars) {
        VEdgeType type = VEdgeType::ET_CHANGED;
        if (!m_edgeAsLevel && !m_edgeAsPrev) {
            for (const Edge& edge : edges) {
                if (edge.varp == varp) type = edge.type;
            }
        }
        AstSenItem* const itemp
            = new AstSenItem{process.flp, type, new AstVarRef{process.flp, varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    // An element designator denotes a subelement signal: the process runs when that
    // element changes, so the element expression is its sensitivity item
    bool sensBad = false;
    for (const ExprP& elemp : process.sensitivityElems) {
        const Expr* namep = elemp.get();
        while (namep && namep->kind != Expr::NAME) namep = namep->a.get();
        if (namep && (varNames.count(namep->text) || m_variables.count(namep->text))) {
            process.flp->v3error("VHDL: sensitivity element '" + elemp->spelling
                                 + "' is not a signal.");
            sensBad = true;
            break;
        }
        AstNodeExpr* const exprp = lowerDesignator(*elemp, false);
        if (!exprp) {
            sensBad = true;
            break;
        }
        AstSenItem* const itemp = new AstSenItem{process.flp, VEdgeType::ET_CHANGED, exprp};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    if (sensBad) {
        if (itemsp) itemsp->deleteTree();
        return nullptr;
    }
    return itemsp;
}

//! The asynchronous-reset flip-flop idiom a process writes:
//! `if rst = <active> then <reset values> elsif rising_edge(clk) then <clocked> end if`.
//! Emitting it as an edge-triggered always puts the clock in the block's sensitivity,
//! where the scheduler keeps it, and the level test inside the body cannot be folded away.
bool Lowering::lowerFlopIdiom(const Process& process) {
    if (process.body.size() != 1 || !process.body[0]) return false;
    const Stmt& ifStmt = *process.body[0];
    if (ifStmt.kind != Stmt::IF || ifStmt.branches.size() != 2) return false;
    // The test and the edge are usually written in parentheses: `if (rst = '0') then`
    const auto unwrap = [](const Expr* exprp) {
        while (exprp && exprp->kind == Expr::PAREN && exprp->a) exprp = exprp->a.get();
        return exprp;
    };
    const Expr* const testp = unwrap(ifStmt.branches[0].first.get());  // `rst = '0'`
    const Expr* const edgep = unwrap(ifStmt.branches[1].first.get());  // `rising_edge(clk)`
    if (!testp || !edgep) return false;
    if (edgep->kind != Expr::INDEX || !edgep->a || edgep->a->kind != Expr::NAME
        || edgep->args.size() != 1 || !edgep->args[0] || edgep->args[0]->kind != Expr::NAME) {
        return false;
    }
    const bool rising = edgep->a->text == "rising_edge";
    if (!rising && edgep->a->text != "falling_edge") return false;
    if (testp->kind != Expr::BINARY || (testp->text != "=" && testp->text != "/=") || !testp->a
        || !testp->b) {
        return false;
    }
    const Expr* namep = nullptr;
    const Expr* levelp = nullptr;
    if (unwrap(testp->a.get())->kind == Expr::NAME) {
        namep = unwrap(testp->a.get());
        levelp = unwrap(testp->b.get());
    } else if (unwrap(testp->b.get())->kind == Expr::NAME) {
        namep = unwrap(testp->b.get());
        levelp = unwrap(testp->a.get());
    } else {
        return false;
    }
    if (levelp->kind != Expr::CHAR_LIT || (levelp->charValue != '0' && levelp->charValue != '1')) {
        return false;
    }
    // The process must be sensitive to exactly the reset and the clock, so that those two
    // edges are all it can be woken by
    const std::string& resetName = namep->text;
    const std::string& clockName = edgep->args[0]->text;
    if (process.sensitivity.size() != 2 || resetName == clockName
        || std::find(process.sensitivity.begin(), process.sensitivity.end(), resetName)
               == process.sensitivity.end()
        || std::find(process.sensitivity.begin(), process.sensitivity.end(), clockName)
               == process.sensitivity.end()) {
        return false;
    }
    Expr resetNameExpr{Expr::NAME};
    resetNameExpr.flp = process.flp;
    resetNameExpr.text = resetName;
    resetNameExpr.spelling = namep->spelling;
    AstVar* const resetVarp = lookupVar(resetNameExpr);
    Expr clockNameExpr{Expr::NAME};
    clockNameExpr.flp = process.flp;
    clockNameExpr.text = clockName;
    clockNameExpr.spelling = edgep->args[0]->spelling;
    AstVar* const clockVarp = lookupVar(clockNameExpr);
    if (!resetVarp || !clockVarp) return false;
    // `rst = '0'` and `rst /= '1'` test the active level, `rst = '1'` and `rst /= '0'` the
    // other one
    const bool activeLow = (levelp->charValue == '0') == (testp->text == "=");
    // The edge test is the sensitivity now, so the branches never see it; an edge call
    // that remains in a branch is that signal's level on the edge
    m_edgeAsLevel = true;
    m_edgeAsPrev = false;
    m_initial = false;
    m_varVals.clear();
    AstNodeStmt* const clockedp = lowerStmtList(ifStmt.branches[1].second);
    AstNodeStmt* const resetp = lowerStmtList(ifStmt.branches[0].second);
    AstNodeExpr* const condp = lowerCond(*testp);
    if (!clockedp || !resetp || !condp) {
        if (clockedp) clockedp->deleteTree();
        if (resetp) resetp->deleteTree();
        if (condp) condp->deleteTree();
        return true;  // The failure is reported; the caller must not lower the body again
    }
    AstSenItem* itemsp
        = new AstSenItem{process.flp, rising ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE,
                         new AstVarRef{process.flp, clockVarp, VAccess::READ}};
    itemsp = AstNode::addNext<AstSenItem, AstSenItem>(
        itemsp,
        new AstSenItem{process.flp, activeLow ? VEdgeType::ET_NEGEDGE : VEdgeType::ET_POSEDGE,
                       new AstVarRef{process.flp, resetVarp, VAccess::READ}});
    AstNodeStmt* const bodyp = new AstIf{process.flp, condp, resetp, clockedp};
    m_modp->addStmtsp(new AstAlways{process.flp, VAlwaysKwd::ALWAYS,
                                    new AstSenTree{process.flp, itemsp}, bodyp});
    // A VHDL process runs once at time zero, when the clock has not had an edge: only a
    // reset that is already active assigns anything
    m_initial = true;
    m_varVals.clear();
    AstNodeStmt* const initp = lowerStmtList(process.body);
    m_initial = false;
    if (initp) m_modp->addStmtsp(new AstInitialStatic{process.flp, initp});
    return true;
}

Lowering::ScopeSave Lowering::saveScope() const {
    ScopeSave saved;
    saved.scope = m_scope;
    saved.consts = m_consts;
    saved.preConsts = m_preConsts;
    saved.types = m_types;
    saved.ambiguousTypes = m_ambiguousTypes;
    saved.ambiguousLiterals = m_ambiguousLiterals;
    saved.constValues = m_constValues;
    saved.constTypes = m_constTypes;
    saved.enumConsts = m_enumConsts;
    saved.subprograms = m_subprograms;
    saved.components = m_components;
    return saved;
}

void Lowering::restoreScope(const ScopeSave& saved) {
    m_scope = saved.scope;
    m_consts = saved.consts;
    m_preConsts = saved.preConsts;
    m_types = saved.types;
    m_ambiguousTypes = saved.ambiguousTypes;
    m_ambiguousLiterals = saved.ambiguousLiterals;
    m_constValues = saved.constValues;
    m_constTypes = saved.constTypes;
    m_enumConsts = saved.enumConsts;
    m_subprograms = saved.subprograms;
    m_components = saved.components;
}

//! Declare the types, constants and variables of a process declarative part
void Lowering::emitProcessDecls(const Process& process, std::set<std::string>& varNames) {
    // A type or subtype in the same declarative part may name a constant declared there
    preScanIntConstants(process.decls);
    registerTypes(process.types, process.decls);
    registerSubprograms(process.subprograms, -1, true);
    // An alias names a designator, which may be a signal the loop below declares
    for (const AliasDecl& alias : process.aliases) {
        if (!alias.hasSignature) registerAlias(alias);
    }
    for (const Decl& decl : process.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectUnmodelledObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            // Mangle so the same variable name in two processes stays distinct
            const std::string mangled = "__Vvar_" + std::to_string(++m_varSeq) + "__" + name.first;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
            traceDeclared(varp);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            varNames.insert(name.first);
            m_variables.insert(name.first);
            if (!decl.init) continue;
            AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
            AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
            if (!rhsp) {
                lhsp->deleteTree();
                continue;
            }
            if (!valueConforms(type, *decl.init, "initial value")) {
                rhsp->deleteTree();
                lhsp->deleteTree();
                continue;
            }
            rhsp = resizeExpr(rhsp, type, decl.flp);
            m_modp->addStmtsp(new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
        }
    }
    finishDeferredTypes();
}

//! Register a declared name; a local declaration hides a name from a used package
bool Lowering::reserveName(const std::string& folded, const std::string& spelling, FileLine* flp) {
    if (m_imported.erase(folded)) {  // The local declaration shadows the package member
        m_consts.erase(folded);
        m_constValues.erase(folded);
        m_constTypes.erase(folded);
        m_enumConsts.erase(folded);
        m_ambiguousLiterals.erase(folded);
        m_realConsts.erase(folded);
        m_types.erase(folded);
        return true;
    }
    if (m_scope.count(folded) || m_consts.count(folded) || m_constValues.count(folded)
        || m_enumConsts.count(folded) || m_realConsts.count(folded) || m_types.count(folded)) {
        flp->v3error("VHDL: duplicate declaration of '" + spelling + "'.");
        return false;
    }
    return true;
}

}  // namespace vhdlnative
