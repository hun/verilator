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
//
// In-tree VHDL frontend. This replaces the GHDL XML importer with a hand-written
// lexer/parser and a lowerer that emits the committed (post-width) Verilator AST
// directly, so GHDL is not invoked.
//
// Structure: Lexer -> Parser (frontend syntax tree) -> Lowering (Verilator AST).
// The parse of every file completes before any lowering, because VHDL design
// units are order independent and an architecture may name an entity declared in
// a later file.
//
// Implemented subset (see vhdl_synth_subset.md for the target set):
//   - entity with ports; architecture with signal/constant declarations
//   - scalar std_logic/std_ulogic/bit/boolean, integer subtypes, constrained
//     std_logic_vector/std_ulogic_vector/bit_vector/unsigned/signed
//   - concurrent signal assignments
//   - processes with an explicit sensitivity list, if/elsif/else, signal
//     assignments, rising_edge/falling_edge clock idioms (including an
//     asynchronous reset branch)
//   - literals, names, indexing/slicing with a statically known width,
//     arithmetic/logic/relational/concat operators, vector conversions,
//     `(others => ...)` aggregates, static integer expressions
// Everything else reports `VHDL ... is not supported yet` with the source
// location.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNative.h"

#include "V3Error.h"
#include "V3FileLine.h"
#include "V3Global.h"
#include "V3VhdlNativeAst.h"
#include "V3VhdlNativeLower.h"

#include <map>
#include <set>
#include <string>
#include <vector>

VL_DEFINE_DEBUG_FUNCTIONS;

namespace vhdlnative {

void unsupportedError(FileLine* flp, const std::string& what) {
    flp->v3error("VHDL " + what + " is not supported yet");
}

}  // namespace vhdlnative

//######################################################################
// V3VhdlNative

void V3VhdlNative::emit(AstNetlist* rootp, const std::vector<V3VhdlNativeFile>& files) {
    const int errorsBefore = V3Error::errorCount();
    vhdlnative::DesignLibrary library;
    for (const V3VhdlNativeFile& file : files) vhdlnative::parseFile(file, library);
    // A parse error may have dropped design units, so elaboration would report spurious
    // missing-architecture and unknown-entity errors on top of the real diagnostic
    if (V3Error::errorCount() != errorsBefore) return;

    // Bind each entity to one architecture; the last analyzed one is the default
    std::map<std::string, const vhdlnative::Architecture*> archByEntity;
    for (const vhdlnative::Architecture& arch : library.archs) {
        archByEntity[arch.entityName] = &arch;
    }

    vhdlnative::PackageTable packages;
    vhdlnative::lowerPackages(library.packages, rootp, packages);

    std::set<std::string> entityNames;
    for (const vhdlnative::EntityUnit& entity : library.entities) {
        if (!entityNames.insert(entity.name).second) {
            entity.flp->v3error("Duplicate VHDL entity: '" + entity.spelling + "'");
            continue;
        }
        const auto it = archByEntity.find(entity.name);
        if (it == archByEntity.end()) {
            entity.flp->v3error("Entity '" + entity.spelling
                                + "' has no architecture; cannot elaborate it");
            continue;
        }
        vhdlnative::lower(entity, *it->second, rootp, packages);
    }
    for (const vhdlnative::Architecture& arch : library.archs) {
        if (!entityNames.count(arch.entityName)) {
            arch.flp->v3error("VHDL architecture does not name a declared entity");
        }
    }
}
