// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL frontend driver
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

#ifndef VERILATOR_V3VHDLPARSE_H_
#define VERILATOR_V3VHDLPARSE_H_

#include "config_build.h"
#include "verilatedos.h"

#include <string>

class AstNetlist;
class FileLine;
class V3VhdlParseImp;

//! VHDL frontend driver.
//!
//! VHDL design units are order-independent, so parseFile() only collects them into a
//! frontend-internal design library. finish() then analyzes and elaborates that library and
//! emits Verilator AST nodes. finish() must run after all files and before V3LinkCells.
class V3VhdlParse final {
    V3VhdlParseImp* m_impp;  // Implementation
    VL_UNCOPYABLE(V3VhdlParse);

public:
    explicit V3VhdlParse(AstNetlist* rootp) VL_MT_DISABLED;
    ~V3VhdlParse() VL_MT_DISABLED;

    //! Parse one file into the design library; does not touch the Verilator netlist
    void parseFile(FileLine* fileline, const std::string& filename, bool inLibrary, bool inLibMap,
                   const std::string& libname, const std::string& errmsg,
                   const std::string& notFoundName = "") VL_MT_DISABLED;
    //! Analyze and elaborate all design units, then emit AstModules
    void finish() VL_MT_DISABLED;
};

#endif  // Guard
