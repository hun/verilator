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
// In-tree VHDL frontend. It is the only VHDL frontend: a hand-written
// lexer/parser and a lowerer that emit the committed (post-width) Verilator AST
// directly, so no external analyzer is invoked.
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
// Everything else reports `Unsupported: VHDL ...` with the source
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
    flp->v3error("Unsupported: VHDL " + what);
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

//! The entity names an architecture instantiates, searching nested statement bodies
void collectInstances(const std::vector<std::unique_ptr<vhdlnative::Concurrent>>& stmts,
                      std::set<std::string>& out) {
    for (const std::unique_ptr<vhdlnative::Concurrent>& statement : stmts) {
        if (!statement) continue;
        if (statement->kind == vhdlnative::Concurrent::INSTANCE)
            out.insert(statement->instanceName);
        collectInstances(statement->body, out);
        for (const vhdlnative::GenBranch& branch : statement->branches) {
            collectInstances(branch.stmts, out);
        }
    }
}

//! The (entity, architecture) pairs an architecture selects, searching nested bodies
void collectArchSelections(const std::vector<std::unique_ptr<vhdlnative::Concurrent>>& stmts,
                           std::set<std::pair<std::string, std::string>>& out) {
    for (const std::unique_ptr<vhdlnative::Concurrent>& statement : stmts) {
        if (!statement) continue;
        if (statement->kind == vhdlnative::Concurrent::INSTANCE
            && !statement->architectureName.empty()) {
            out.emplace(statement->instanceName, statement->architectureName);
        }
        collectArchSelections(statement->body, out);
        for (const vhdlnative::GenBranch& branch : statement->branches) {
            collectArchSelections(branch.stmts, out);
        }
    }
}

//! Visit the entities the named one instantiates, then the named one itself, so no entity is
//! elaborated before a child it instantiates. `visiting` breaks a cycle, which VHDL forbids
//! and the frontend then reports as an instance whose child has no port.
void orderVisit(const std::string& name,
                const std::map<std::string, const vhdlnative::EntityUnit*>& byName,
                const std::map<std::string, std::set<std::string>>& instantiatedBy,
                std::set<std::string>& done, std::set<std::string>& visiting,
                std::vector<const vhdlnative::EntityUnit*>& order) {
    if (!done.insert(name).second || !visiting.insert(name).second) return;
    const auto childIt = instantiatedBy.find(name);
    if (childIt != instantiatedBy.end()) {
        for (const std::string& child : childIt->second) {
            orderVisit(child, byName, instantiatedBy, done, visiting, order);
        }
    }
    visiting.erase(name);
    const auto entityIt = byName.find(name);
    if (entityIt != byName.end()) order.push_back(entityIt->second);
}

//! Append the uses of a context declaration, following nested context references
void appendContextUses(const std::string& name,
                       const std::map<std::string, const vhdlnative::ContextUnit*>& contexts,
                       std::set<std::string>& visiting, std::vector<vhdlnative::UseClause>& uses) {
    const auto it = contexts.find(name);
    if (it == contexts.end()) return;  // An unknown context is left for the unit to report
    if (!visiting.insert(name).second) return;  // VHDL forbids a context cycle
    for (const auto& use : it->second->uses) uses.push_back(use);
    for (const std::string& nested : it->second->contextNames) {
        appendContextUses(nested, contexts, visiting, uses);
    }
    visiting.erase(name);
}

//! Make every referenced context's packages visible to the referencing design unit
void expandContexts(vhdlnative::DesignLibrary& library) {
    std::map<std::string, const vhdlnative::ContextUnit*> byName;
    for (const vhdlnative::ContextUnit& context : library.contexts) {
        byName[context.name] = &context;
    }
    const auto expand
        = [&](const std::vector<std::string>& names, std::vector<vhdlnative::UseClause>& uses) {
              std::set<std::string> visiting;
              for (const std::string& name : names) {
                  appendContextUses(name, byName, visiting, uses);
              }
          };
    for (vhdlnative::EntityUnit& entity : library.entities) {
        expand(entity.contextNames, entity.uses);
    }
    for (vhdlnative::Architecture& arch : library.archs) expand(arch.contextNames, arch.uses);
    for (vhdlnative::PackageUnit& pkg : library.packages) expand(pkg.contextNames, pkg.uses);
    for (vhdlnative::PackageInstance& inst : library.packageInstances) {
        expand(inst.contextNames, inst.uses);
    }
}

}  // namespace

void V3VhdlNative::emit(AstNetlist* rootp, const std::vector<V3VhdlNativeFile>& files) {
    const int errorsBefore = V3Error::errorCount();
    vhdlnative::DesignLibrary library;
    for (const V3VhdlNativeFile& file : files) vhdlnative::parseFile(file, library);
    // A parse error may have dropped design units, so elaboration would report spurious
    // missing-architecture and unknown-entity errors on top of the real diagnostic
    if (V3Error::errorCount() != errorsBefore) return;
    expandContexts(library);

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
                    binding.flp->v3error("Unsupported: VHDL configuration '" + config.spelling
                                         + "' binds component '" + binding.component
                                         + "'; component instantiation");
                } else if (instp->instanceName != binding.entity) {
                    binding.flp->v3error("VHDL configuration '" + config.spelling
                                         + "' rebinds instance '" + label + "' to entity '"
                                         + binding.entity + "', not to the '" + instp->instanceName
                                         + "' it instantiates");
                }
            }
        }
    }

    // A physical literal names its unit but not its type, and a generic default lowers
    // before the declarations around it, so the unit names are read from the library first
    vhdlnative::Design design;
    vhdlnative::collectPhysicalUnits(library, design);

    vhdlnative::PackageTable packages;
    vhdlnative::lowerPackages(library, rootp, packages, design);

    // Create the module of every entity first, so instances can bind to any of them
    vhdlnative::ModuleMap modules;
    vhdlnative::createModules(library.entities, rootp, modules);
    for (const vhdlnative::EntityUnit& entity : library.entities) {
        design.entities[entity.name] = &entity;
    }
    for (const auto& entry : archByEntity) design.archs[entry.first] = entry.second;
    for (const vhdlnative::Architecture& arch : library.archs) {
        design.archsByName[arch.entityName][arch.name] = &arch;
    }
    // Record ports whose element widths come from the actual, like unconstrained arrays
    vhdlnative::collectUnconstrainedRecordMembers(library, design.unconstrainedRecordMembers);
    // An instance that names an architecture gets a module lowered with it. An entity whose
    // base module needs instance bindings is specialized instead, and that path picks the
    // architecture per instance.
    std::set<std::pair<std::string, std::string>> archSelections;
    for (const vhdlnative::Architecture& arch : library.archs) {
        collectArchSelections(arch.stmts, archSelections);
    }
    for (const auto& selection : archSelections) {
        const auto entityIt = design.entities.find(selection.first);
        if (entityIt == design.entities.end()) continue;  // Reported at the instance
        const auto archIt = design.archsByName[selection.first].find(selection.second);
        if (archIt == design.archsByName[selection.first].end()) continue;  // Reported
        if (vhdlnative::entityNeedsSpecialization(*entityIt->second, design)) continue;
        const auto defaultIt = design.archs.find(selection.first);
        if (defaultIt != design.archs.end() && defaultIt->second->name == selection.second) {
            continue;  // The base module already uses this architecture
        }
        const std::string moduleName
            = selection.first + "__Varch" + std::to_string(++design.specSeq);
        AstModule* const modp = new AstModule{archIt->second->flp, moduleName,
                                              archIt->second->libname};
        modp->inLibrary(archIt->second->inLibrary);
        rootp->addModulesp(modp);
        modules[moduleName] = modp;
        design.archModules[selection] = modp;
    }

    // Elaborate every entity after the entities its architecture instantiates: an instance
    // binds the ports its child's own elaboration declares, so in source order a user written
    // first would bind a module whose ports are not there yet.
    std::map<std::string, const vhdlnative::EntityUnit*> entityByName;
    std::map<std::string, std::set<std::string>> instantiatedBy;
    for (const vhdlnative::EntityUnit& entity : library.entities) {
        entityByName.emplace(entity.name, &entity);
    }
    // Every architecture of an entity contributes dependencies: an instance may select one
    // that the default architecture does not use
    for (const vhdlnative::Architecture& arch : library.archs) {
        if (entityByName.count(arch.entityName)) {
            collectInstances(arch.stmts, instantiatedBy[arch.entityName]);
        }
    }
    std::vector<const vhdlnative::EntityUnit*> order;
    {
        std::set<std::string> done;
        std::set<std::string> visiting;
        for (const vhdlnative::EntityUnit& entity : library.entities) {
            if (entityByName[entity.name] != &entity) {
                order.push_back(&entity);  // A second entity of this name; reported below
                continue;
            }
            if (!done.count(entity.name)) {
                orderVisit(entity.name, entityByName, instantiatedBy, done, visiting, order);
            }
        }
    }
    std::set<std::string> entityNames;
    std::set<std::string> deletedModules;  // Base modules no instance ends up binding to
    std::vector<const vhdlnative::EntityUnit*> unelaborated;
    std::vector<const vhdlnative::EntityUnit*> portUnelaborated;
    for (const vhdlnative::EntityUnit* const entryp : order) {
        const vhdlnative::EntityUnit& entity = *entryp;
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
        if (vhdlnative::hasUnconstrainedPorts(entity, design.unconstrainedRecordMembers)) {
            // Only an instance's actuals fix the port ranges
            portUnelaborated.push_back(&entity);
            continue;
        }
        if (!vhdlnative::lower(entity, *it->second, modules[entity.name], packages, modules,
                               design)) {
            unelaborated.push_back(&entity);
            continue;
        }
        // Lower every additional architecture an instance selects, with the same generic
        // defaults the base module used
        for (const auto& entry : design.archModules) {
            if (entry.first.first != entity.name) continue;
            const auto byName = design.archsByName.find(entity.name);
            if (byName == design.archsByName.end()) continue;
            const auto archIt = byName->second.find(entry.first.second);
            if (archIt == byName->second.end()) continue;
            vhdlnative::lower(entity, *archIt->second, entry.second, packages, modules, design);
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
            deletedModules.insert(entityp->name);
        } else if (design.instantiated.count(entityp->name)) {
            entityp->flp->v3error("VHDL: entity '" + entityp->spelling
                                  + "' has a generic without a default value, and no instance "
                                    "binds it");
        } else if (modules[entityp->name]) {
            // An entity no instance names is not part of the design: nothing binds its
            // generics, and an empty module would look like another top
            deletedModules.insert(entityp->name);
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
            deletedModules.insert(entityp->name);
        } else {
            vhdlnative::reportUnspecializedPorts(*entityp, design.unconstrainedRecordMembers);
        }
    }
    // A base module no instance binds to is dead: it has no parent and would look like
    // another top. An instance that binds nothing uses the base itself.
    for (const auto& entry : modules) {
        if (!design.instantiated.count(entry.first)) continue;
        if (design.baseUsed.count(entry.first)) continue;
        deletedModules.insert(entry.first);
    }
    for (const std::string& name : deletedModules) {
        if (AstModule* const modp = modules[name]) {
            modules[name] = nullptr;
            VL_DO_DANGLING(modp->unlinkFrBack()->deleteTree(), modp);
        }
    }
}
