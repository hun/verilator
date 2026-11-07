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

#ifndef VERILATOR_V3VHDLLINK_H_
#define VERILATOR_V3VHDLLINK_H_

#include "config_build.h"
#include "verilatedos.h"

class AstNetlist;

//! VHDL-specific binding and control-file application.
//!
//! VHDL mode skips V3LinkParse, which normally applies the control-file directives that
//! target functions, variables, blocks and cases. This pass applies those directives to the
//! already-emitted VHDL AST and checks the post-width AST contract in debug builds.
class V3VhdlLink final {
public:
    static void link(AstNetlist* nodep) VL_MT_DISABLED;
};

#endif  // Guard
