// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: lowering
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

#ifndef VERILATOR_V3VHDLNATIVE_LOWER_H_
#define VERILATOR_V3VHDLNATIVE_LOWER_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3VhdlNativeAst.h"

class AstNetlist;

namespace vhdlnative {

//! Lower one entity/architecture pair to a module appended to the netlist
void lower(const EntityUnit& entity, const Architecture& arch, AstNetlist* rootp) VL_MT_DISABLED;

}  // namespace vhdlnative

#endif  // Guard
