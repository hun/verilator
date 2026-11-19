// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL-specific checks after V3LinkCells
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

#include "V3Global.h"

VL_DEFINE_DEBUG_FUNCTIONS;

void V3VhdlLink::link(AstNetlist* nodep, const std::set<const AstNodeModule*>& vhdlModules) {
    if (!v3Global.opt.debugCheck()) return;
    // Only the modules this frontend emitted are subject to its contract; a mixed design
    // also holds SystemVerilog modules, whose nodes the width pass has not seen yet
    for (const AstNodeModule* const modp : vhdlModules) {
        const_cast<AstNodeModule*>(modp)->foreach([](AstNode* np) {
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
}
