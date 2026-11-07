// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL-specific binding after V3LinkCells
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

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlLink.h"

#include "V3Control.h"
#include "V3Global.h"

VL_DEFINE_DEBUG_FUNCTIONS;

namespace {

//######################################################################
// Apply the control-file directives that V3LinkParse would have applied

class VhdlLinkVisitor final : public VNVisitor {
    AstNodeModule* m_modp = nullptr;  // Current module or package

    void visit(AstNodeModule* nodep) override {
        VL_RESTORER(m_modp);
        m_modp = nodep;
        iterateChildren(nodep);
    }
    void visit(AstVar* nodep) override {
        if (m_modp) V3Control::applyVarAttr(m_modp, nullptr, nodep);
        iterateChildren(nodep);
    }
    void visit(AstNodeFTask* nodep) override {
        if (m_modp) V3Control::applyFTask(m_modp, nodep);
        iterateChildren(nodep);
    }
    void visit(AstBegin* nodep) override {
        if (m_modp) V3Control::applyCoverageBlock(m_modp, nodep);
        iterateChildren(nodep);
    }
    void visit(AstGenBlock* nodep) override {
        if (m_modp) V3Control::applyCoverageBlock(m_modp, nodep);
        iterateChildren(nodep);
    }
    void visit(AstCase* nodep) override {
        V3Control::applyCase(nodep);
        iterateChildren(nodep);
    }
    void visit(AstNode* nodep) override { iterateChildren(nodep); }

public:
    explicit VhdlLinkVisitor(AstNetlist* nodep) { iterate(nodep); }
    ~VhdlLinkVisitor() override = default;
};

}  // namespace

void V3VhdlLink::link(AstNetlist* nodep) {
    { VhdlLinkVisitor{nodep}; }

    if (!v3Global.opt.debugCheck()) return;
    nodep->foreach([](AstNode* np) {
        UASSERT_OBJ(!VN_IS(np, ParseRef), np, "AstParseRef in committed VHDL AST");
        UASSERT_OBJ(!np->getChildDTypep(), np, "childDTypep in committed VHDL AST");
        if (AstNodeExpr* const ep = VN_CAST(np, NodeExpr)) {
            UASSERT_OBJ(ep->dtypep(), np, "Expression without dtype in committed VHDL AST");
        }
        if (AstVar* const vp = VN_CAST(np, Var)) {
            UASSERT_OBJ(vp->dtypep(), np, "Variable without dtype in committed VHDL AST");
        }
    });
}
