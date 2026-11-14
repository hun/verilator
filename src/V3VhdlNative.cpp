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

#include <algorithm>
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

namespace {

//! The labels of a comma separated instantiation list
std::vector<std::string> splitLabels(const std::string& list) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t comma = list.find(',', begin);
        const std::size_t end = comma == std::string::npos ? list.size() : comma;
        if (end > begin) out.push_back(list.substr(begin, end - begin));
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return out;
}

//! The instance of the given label, searching a statement list and its nested bodies
const vhdlnative::Concurrent*
findInstance(const std::vector<std::unique_ptr<vhdlnative::Concurrent>>& stmts,
             const std::string& label) {
    for (const std::unique_ptr<vhdlnative::Concurrent>& statement : stmts) {
        if (!statement) continue;
        if (statement->kind == vhdlnative::Concurrent::INSTANCE
            && statement->instanceLabel == label) {
            return statement.get();
        }
        const vhdlnative::Concurrent* const foundp = findInstance(statement->body, label);
        if (foundp) return foundp;
    }
    return nullptr;
}

}  // namespace

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
    // A configuration binds component specifications; it changes nothing when each binding
    // names the entity the architecture already instantiates directly
    for (const vhdlnative::ConfigurationUnit& config : library.configurations) {
        const auto archIt = archByEntity.find(config.entityName);
        if (archIt == archByEntity.end()) {
            config.flp->v3error("VHDL configuration '" + config.spelling + "' configures entity '"
                                + config.entityName + "', which has no architecture");
            continue;
        }
        if (archIt->second->name != config.archName) {
            config.flp->v3error("VHDL configuration '" + config.spelling + "' names architecture '"
                                + config.archName + "', but the analyzed one is '"
                                + archIt->second->name + "'");
            continue;
        }
        for (const vhdlnative::ConfigBinding& binding : config.bindings) {
            for (const std::string& label : splitLabels(binding.instances)) {
                const vhdlnative::Concurrent* instp = findInstance(archIt->second->stmts, label);
                if (!instp) {
                    binding.flp->v3error("VHDL configuration '" + config.spelling
                                         + "' binds instance '" + label
                                         + "', which the architecture does not instantiate");
                } else if (!instp->entityInstantiation) {
                    binding.flp->v3error("VHDL configuration '" + config.spelling
                                         + "' binds component '" + binding.component
                                         + "'; component instantiation is not supported yet");
                } else if (instp->instanceName != binding.entity) {
                    binding.flp->v3error("VHDL configuration '" + config.spelling
                                         + "' rebinds instance '" + label + "' to entity '"
                                         + binding.entity + "', not to the '" + instp->instanceName
                                         + "' it instantiates");
                }
            }
        }
    }

    vhdlnative::PackageTable packages;
    vhdlnative::lowerPackages(library, rootp, packages);

    // Create the module of every entity first, so instances can bind to any of them
    vhdlnative::ModuleMap modules;
    vhdlnative::createModules(library.entities, rootp, modules);
    vhdlnative::Design design;
    for (const vhdlnative::EntityUnit& entity : library.entities) {
        design.entities[entity.name] = &entity;
    }
    for (const auto& entry : archByEntity) design.archs[entry.first] = entry.second;
    // Record ports whose element widths come from the actual, like unconstrained arrays
    vhdlnative::collectUnconstrainedRecordMarks(library, design.unconstrainedRecordMarks);

    std::set<std::string> entityNames;
    std::vector<const vhdlnative::EntityUnit*> unelaborated;
    std::vector<const vhdlnative::EntityUnit*> portUnelaborated;
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
        if (vhdlnative::hasUnconstrainedPorts(entity, design.unconstrainedRecordMarks)) {
            // Only an instance's actuals fix the port ranges
            portUnelaborated.push_back(&entity);
            continue;
        }
        if (!vhdlnative::lower(entity, *it->second, modules[entity.name], packages, modules,
                               design)) {
            unelaborated.push_back(&entity);
        }
    }
    for (const vhdlnative::Architecture& arch : library.archs) {
        if (!entityNames.count(arch.entityName)) {
            arch.flp->v3error("VHDL architecture does not name a declared entity");
        }
    }
    // An entity whose generics have no defaults elaborates only through instances
    for (const vhdlnative::EntityUnit* const entityp : unelaborated) {
        const std::string prefix = entityp->name + "|";
        const bool covered = std::any_of(
            design.specializations.begin(), design.specializations.end(), [&](const auto& entry) {
                return entry.second && entry.first.compare(0, prefix.size(), prefix) == 0;
            });
        if (covered) {
            // Only the specializations are referenced; the empty base would look like a top
            AstModule* const modp = modules[entityp->name];
            VL_DO_DANGLING(modp->unlinkFrBack()->deleteTree(), modp);
        } else {
            entityp->flp->v3error("VHDL: entity '" + entityp->spelling
                                  + "' has a generic without a default value, and no instance "
                                    "binds it");
        }
    }
    // An entity with unconstrained ports elaborates only through its instances
    for (const vhdlnative::EntityUnit* const entityp : portUnelaborated) {
        const std::string prefix = entityp->name + "|ports";
        const bool covered = std::any_of(
            design.specializations.begin(), design.specializations.end(), [&](const auto& entry) {
                return entry.second && entry.first.compare(0, prefix.size(), prefix) == 0;
            });
        if (covered) {
            AstModule* const modp = modules[entityp->name];
            VL_DO_DANGLING(modp->unlinkFrBack()->deleteTree(), modp);
        } else {
            vhdlnative::reportUnspecializedPorts(*entityp, design.unconstrainedRecordMarks);
        }
    }
}
