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

#include <set>

class AstNetlist;
class AstNodeModule;

//! VHDL-specific check of the AST the frontend emitted.
//!
//! The control-file directives are applied by V3LinkParse, which now runs on VHDL modules
//! too. What remains is the post-width contract of the emitted AST, checked in debug builds
//! and only on the modules this frontend produced, since a mixed design also holds
//! SystemVerilog modules that are still in parse shape at this point.
class V3VhdlLink final {
public:
    static void link(AstNetlist* nodep,
                     const std::set<const AstNodeModule*>& vhdlModules) VL_MT_DISABLED;
};

#endif  // Guard
