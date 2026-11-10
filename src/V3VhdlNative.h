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

#ifndef VERILATOR_V3VHDLNATIVE_H_
#define VERILATOR_V3VHDLNATIVE_H_

#include "config_build.h"
#include "verilatedos.h"

#include <string>
#include <vector>

class AstNetlist;

//! One VHDL design file collected by the frontend driver.
struct V3VhdlNativeFile final {
    std::string filename;  // Resolved path
    std::string libname;  // Library the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
};

//! In-tree VHDL frontend: parses VHDL directly and lowers it to the committed
//! (post-width) Verilator AST that V3VhdlLink expects. It does not invoke an
//! external analyzer. The accepted language subset is documented in
//! vhdl_synth_subset.md; everything outside the implemented part of it is
//! rejected with a source-located `Unsupported:` diagnostic.
class V3VhdlNative final {
public:
    //! Parse all files, elaborate, and append AstModules to the netlist
    static void emit(AstNetlist* rootp, const std::vector<V3VhdlNativeFile>& files) VL_MT_DISABLED;
};

#endif  // Guard
