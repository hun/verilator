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
// SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
//
// VHDL frontend driver: collects design files, then runs the in-tree frontend
// (V3VhdlNative). VHDL design units are order independent, so files are only
// collected while the sources are read; the frontend parses and elaborates the
// whole library when finish() runs.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlParse.h"

#include "V3FileLine.h"
#include "V3Global.h"
#include "V3Os.h"
#include "V3Stats.h"
#include "V3VhdlNative.h"

#include <set>
#include <string>
#include <vector>

namespace {

//! One VHDL source file handed to the frontend
struct VhdlFile final {
    std::string filename;  // Resolved path
    std::string libname;  // Library the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
};

}  // namespace

//######################################################################
// Implementation state: the frontend design library

class V3VhdlParseImp final {
public:
    AstNetlist* const m_rootp;  // Netlist to emit into
    std::vector<VhdlFile> m_files;  // Input design files

    explicit V3VhdlParseImp(AstNetlist* rootp)
        : m_rootp{rootp} {}
    ~V3VhdlParseImp() = default;

    //! Parse, elaborate and lower the collected files in-tree, recording the modules it adds
    void finish(std::set<AstNodeModule*>& emitted);
};

void V3VhdlParseImp::finish(std::set<AstNodeModule*>& emitted) {
    if (m_files.empty()) return;
    // A mixed design parses SystemVerilog sources into the same netlist, so remember which
    // modules this frontend adds for the checks that only apply to its own AST
    std::set<AstNodeModule*> before;
    for (AstNodeModule* modp = m_rootp->modulesp(); modp;
         modp = VN_AS(modp->nextp(), NodeModule)) {
        before.insert(modp);
    }
    std::vector<V3VhdlNativeFile> files;
    files.reserve(m_files.size());
    for (const VhdlFile& vfile : m_files) {
        files.push_back(V3VhdlNativeFile{vfile.filename, vfile.libname, vfile.inLibrary});
    }
    V3VhdlNative::emit(m_rootp, files);
    // Count modules here: V3LinkParse, which adds the same stat later in the pipeline,
    // has not run yet at finish() time
    uint64_t modules = 0;
    for (AstNodeModule* modp = m_rootp->modulesp(); modp;
         modp = VN_AS(modp->nextp(), NodeModule)) {
        ++modules;
    }
    V3Stats::addStatSum(V3Stats::STAT_SOURCE_MODULES, modules);
    for (AstNodeModule* modp = m_rootp->modulesp(); modp;
         modp = VN_AS(modp->nextp(), NodeModule)) {
        if (!before.count(modp)) emitted.insert(modp);
    }
}

//######################################################################
// V3VhdlParse

V3VhdlParse::V3VhdlParse(AstNetlist* rootp)
    : m_impp{new V3VhdlParseImp{rootp}} {}

V3VhdlParse::~V3VhdlParse() { delete m_impp; }

void V3VhdlParse::parseFile(FileLine* fileline, const std::string& filename, bool inLibrary,
                            bool inLibMap, const std::string& libname, const std::string& errmsg,
                            const std::string& notFoundName) {
    const std::string modname = V3Os::filenameNonDirExt(filename);
    if (v3Global.opt.preprocOnly()) return;  // VHDL has no preprocessor
    if (inLibMap) {
        fileline->v3error(
            "Library map files are not supported for VHDL sources.");  // LCOV_EXCL_LINE
        return;
    }

    const std::string resolved = v3Global.opt.filePath(fileline, filename, "", errmsg);
    if (resolved.empty()) {
        if (errmsg != "") return;  // Error already reported
        AstNodeModule* const nodep = new AstNotFoundModule{
            fileline, notFoundName.empty() ? modname : notFoundName, libname};
        m_impp->m_rootp->addModulesp(nodep);
        return;
    }

    for (const VhdlFile& vfile : m_impp->m_files) {
        if (vfile.filename == resolved) return;  // Already collected
    }
    m_impp->m_files.push_back(VhdlFile{resolved, libname, inLibrary});
}

void V3VhdlParse::finish() { m_impp->finish(m_modules); }
