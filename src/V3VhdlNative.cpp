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
// SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
//
// VHDL frontend: a hand-written lexer/parser and a lowerer that emit an AST with
// committed dtypes before V3Width runs.
//
// Structure: Lexer -> Parser (frontend syntax tree) -> Lowering (Verilator AST).
// The parse of every file completes before any lowering, because VHDL design
// units are order independent and an architecture may name an entity declared in
// a later file.
//
// Implemented subset and known divergences: docs/vhdl/limitations.rst.
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
    flp->v3warn(E_UNSUPPORTED, "Unsupported: VHDL " + what + ".");
}

}  // namespace vhdlnative

//######################################################################
// V3VhdlNative

namespace {

//! The labels of a comma separated instantiation list
std::vector<std::string> splitLabels(const std::string& list) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (true) {
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

//! Entity name -> the last analyzed architecture implementing it
using ArchByEntity = std::map<std::string, const vhdlnative::Architecture*>;

//! State shared by the elaboration steps of one frontend run
struct EmitContext final {
    explicit EmitContext(AstNetlist* netlistp)
        : m_rootp{netlistp} {}
    AstNetlist* const m_rootp;  // Netlist the frontend emits into
    vhdlnative::DesignLibrary library;  // Design units parsed from all files
    vhdlnative::Design design;  // Design context shared by the lowering steps
    vhdlnative::PackageTable packages;  // Elaborated packages
    vhdlnative::ModuleMap modules;  // Module of each entity, by folded name
    ArchByEntity archByEntity;  // Architecture bound to each entity
    // Entities in elaboration order, instantiated entities first
    std::vector<const vhdlnative::EntityUnit*> order;
    std::set<std::string> entityNames;  // Entity names elaborated so far, to reject duplicates
    std::set<std::string> deletedModules;  // Entity names whose base module is removed
    // Entities whose generics lack defaults, elaborated per instance
    std::vector<const vhdlnative::EntityUnit*> unelaborated;
    // Entities with unconstrained ports, elaborated per instance
    std::vector<const vhdlnative::EntityUnit*> portUnelaborated;
};

//! The instances of a statement list and its nested bodies, in source order
void collectInstanceStmts(const std::vector<std::unique_ptr<vhdlnative::Concurrent>>& stmts,
                          std::vector<vhdlnative::Concurrent*>& out) {
    for (const std::unique_ptr<vhdlnative::Concurrent>& statement : stmts) {
        if (!statement) continue;
        if (statement->kind == vhdlnative::Concurrent::INSTANCE) out.push_back(statement.get());
        collectInstanceStmts(statement->body, out);
        for (const vhdlnative::GenBranch& branch : statement->branches) {
            collectInstanceStmts(branch.stmts, out);
        }
    }
}

//! The generic map of a binding indication gives the entity's generics a value of their own,
//! over the one the instance's generic map gave. A name in it that is a generic of the
//! component takes the actual the instance maps to it.
void applyBindingGenericMap(const vhdlnative::ConfigBinding& binding,
                            vhdlnative::Concurrent& instance) {
    for (const vhdlnative::PortAssoc& bound : binding.genericMap) {
        vhdlnative::PortAssoc assoc;
        assoc.flp = bound.flp;
        assoc.formal = bound.formal;
        assoc.formalSpelling = bound.formalSpelling;
        if (bound.actual) {
            assoc.actual = vhdlnative::cloneExpr(*bound.actual);
            if (assoc.actual->kind == vhdlnative::Expr::NAME) {
                for (const vhdlnative::PortAssoc& given : instance.genericAssociations) {
                    if (given.formal == assoc.actual->text && given.actual) {
                        assoc.actual = vhdlnative::cloneExpr(*given.actual);
                        break;
                    }
                }
            }
        }
        const auto sameFormal
            = [&](const vhdlnative::PortAssoc& other) { return other.formal == assoc.formal; };
        const auto it = std::find_if(instance.genericAssociations.begin(),
                                     instance.genericAssociations.end(), sameFormal);
        if (it != instance.genericAssociations.end()) {
            *it = std::move(assoc);
        } else {
            instance.genericAssociations.push_back(std::move(assoc));
        }
    }
}

//! Apply one binding of a configuration to the instances of the architecture it configures
void applyConfigBinding(const vhdlnative::ConfigurationUnit& config,
                        const vhdlnative::ConfigBinding& binding,
                        const std::vector<vhdlnative::Concurrent*>& instances,
                        const std::set<std::string>& namedLabels,
                        std::set<const vhdlnative::Concurrent*>& configured) {
    const std::vector<std::string> labels = splitLabels(binding.instances);
    std::set<std::string> found;
    for (vhdlnative::Concurrent* const instp : instances) {
        const bool listed
            = std::find(labels.begin(), labels.end(), instp->instanceLabel) != labels.end();
        const bool matches
            = binding.all || binding.others ? instp->instanceName == binding.component : listed;
        // `others` leaves the instances that a binding of the configuration names
        if (!matches || (binding.others && namedLabels.count(instp->instanceLabel))) continue;
        found.insert(instp->instanceLabel);
        if (!instp->entityInstantiation && instp->instanceName != binding.entity) {
            // The component is bound to an entity of another name; its ports and generics
            // pair with the entity's by name, as for a default binding
            instp->instanceName = binding.entity;
            applyBindingGenericMap(binding, *instp);
            if (!binding.architecture.empty()) instp->architectureName = binding.architecture;
            configured.insert(instp);
        } else if (instp->entityInstantiation && instp->instanceName != binding.entity) {
            binding.flp->v3error("VHDL configuration '" + config.spelling + "' rebinds instance '"
                                 + instp->instanceLabel + "' to entity '" + binding.entity
                                 + "', not to the '" + instp->instanceName + "' it instantiates.");
        } else if (binding.architecture.empty()) {
            applyBindingGenericMap(binding, *instp);
            continue;
        } else if (configured.count(instp) && instp->architectureName != binding.architecture) {
            vhdlnative::unsupportedError(binding.flp,
                                         "configurations selecting different architectures for "
                                         "instance '"
                                             + instp->instanceLabel + "'");
        } else {
            // A configuration declaration overrides a specification or an instance's own choice
            instp->architectureName = binding.architecture;
            applyBindingGenericMap(binding, *instp);
            configured.insert(instp);
        }
    }
    for (const std::string& label : labels) {
        if (!found.count(label)) {
            binding.flp->v3error("VHDL configuration '" + config.spelling + "' binds instance '"
                                 + label + "', which the architecture does not instantiate.");
        }
    }
}

//! Bind each entity to one architecture and apply the configuration bindings
ArchByEntity resolveArchitectures(vhdlnative::DesignLibrary& library) {
    // The last analyzed architecture is the default
    ArchByEntity archByEntity;
    for (const vhdlnative::Architecture& arch : library.archs) {
        archByEntity[arch.entityName] = &arch;
    }
    // `label : configuration work.X` instantiates the entity of X with the architecture X
    // selects; X's own bindings apply to that entity like any configuration's
    for (vhdlnative::Architecture& arch : library.archs) {
        std::vector<vhdlnative::Concurrent*> instances;
        collectInstanceStmts(arch.stmts, instances);
        for (vhdlnative::Concurrent* const instp : instances) {
            if (!instp->configurationInstantiation) continue;
            instp->configurationInstantiation = false;
            const auto configIt
                = std::find_if(library.configurations.begin(), library.configurations.end(),
                               [&](const vhdlnative::ConfigurationUnit& config) {
                                   return config.name == instp->instanceName;
                               });
            if (configIt == library.configurations.end()) {
                instp->flp->v3error("VHDL: instance '" + instp->instanceLabel
                                    + "' names configuration '" + instp->instanceName
                                    + "', which was not analyzed.");
                continue;
            }
            instp->instanceName = configIt->entityName;
            instp->architectureName = configIt->archName;
        }
    }
    std::set<const vhdlnative::Concurrent*> configured;  // Instances a configuration selected
    // A configuration selects the architecture of the instances it binds. A binding that
    // names no architecture leaves the instance as written.
    for (const vhdlnative::ConfigurationUnit& config : library.configurations) {
        for (const std::string& used : config.usedConfigurations) {
            const bool known = std::any_of(
                library.configurations.begin(), library.configurations.end(),
                [&](const vhdlnative::ConfigurationUnit& other) { return other.name == used; });
            if (!known) {
                config.flp->v3error("VHDL configuration '" + config.spelling
                                    + "' uses configuration '" + used
                                    + "', which was not analyzed.");
            }
        }
        const auto archIt = archByEntity.find(config.entityName);
        if (archIt == archByEntity.end()) {
            config.flp->v3error("VHDL configuration '" + config.spelling + "' configures entity '"
                                + config.entityName + "', which has no architecture.");
            continue;
        }
        // The configuration configures the architecture it names, which need not be the
        // one an instance gets when it names none
        vhdlnative::Architecture* configuredp = nullptr;
        for (vhdlnative::Architecture& arch : library.archs) {
            if (arch.entityName == config.entityName && arch.name == config.archName) {
                configuredp = &arch;
            }
        }
        if (!configuredp) {
            config.flp->v3error("VHDL configuration '" + config.spelling + "' names architecture '"
                                + config.archName + "', which entity '" + config.entityName
                                + "' does not have.");
            continue;
        }
        std::vector<vhdlnative::Concurrent*> instances;
        collectInstanceStmts(configuredp->stmts, instances);
        std::set<std::string> namedLabels;
        for (const vhdlnative::ConfigBinding& binding : config.bindings) {
            for (const std::string& label : splitLabels(binding.instances)) {
                namedLabels.insert(label);
            }
        }
        for (const bool others : {false, true}) {
            for (const vhdlnative::ConfigBinding& binding : config.bindings) {
                if (binding.others == others) {
                    applyConfigBinding(config, binding, instances, namedLabels, configured);
                }
            }
        }
    }
    return archByEntity;
}

//! Lower the declarative parts and create the module of every entity first, so instances
//! can bind to any of them
void prepareModules(EmitContext& ctx) {
    // A physical literal names its unit but not its type, and a generic default lowers
    // before the declarations around it, so the unit names are read from the library first
    vhdlnative::collectPhysicalUnits(ctx.library, ctx.design);
    vhdlnative::lowerPackages(ctx.library, ctx.m_rootp, ctx.packages, ctx.design);
    vhdlnative::createModules(ctx.library.entities, ctx.m_rootp, ctx.modules);
    for (const vhdlnative::EntityUnit& entity : ctx.library.entities) {
        ctx.design.entities[entity.name] = &entity;
    }
    for (const auto& entry : ctx.archByEntity) ctx.design.archs[entry.first] = entry.second;
    for (const vhdlnative::Architecture& arch : ctx.library.archs) {
        ctx.design.archsByName[arch.entityName][arch.name] = &arch;
    }
    // Record ports whose element widths come from the actual, like unconstrained arrays
    vhdlnative::collectUnconstrainedRecordMembers(ctx.library,
                                                  ctx.design.unconstrainedRecordMembers);
}

//! Create a module for each architecture explicitly selected by an instance
void createSelectedArchModules(EmitContext& ctx) {
    // An instance that names an architecture gets a module lowered with it. An entity whose
    // base module needs instance bindings is specialized instead, and that path picks the
    // architecture per instance.
    std::set<std::pair<std::string, std::string>> archSelections;
    for (const vhdlnative::Architecture& arch : ctx.library.archs) {
        collectArchSelections(arch.stmts, archSelections);
    }
    for (const auto& selection : archSelections) {
        const auto entityIt = ctx.design.entities.find(selection.first);
        if (entityIt == ctx.design.entities.end()) continue;  // Reported at the instance
        const auto archIt = ctx.design.archsByName[selection.first].find(selection.second);
        if (archIt == ctx.design.archsByName[selection.first].end()) continue;  // Reported
        if (vhdlnative::entityNeedsSpecialization(*entityIt->second, ctx.design)) continue;
        const auto defaultIt = ctx.design.archs.find(selection.first);
        if (defaultIt != ctx.design.archs.end() && defaultIt->second->name == selection.second) {
            continue;  // The base module already uses this architecture
        }
        const std::string moduleName
            = selection.first + "__Varch" + std::to_string(++ctx.design.specSeq);
        AstModule* const modp
            = new AstModule{archIt->second->flp, moduleName, archIt->second->libname};
        modp->inLibrary(archIt->second->inLibrary);
        ctx.m_rootp->addModulesp(modp);
        ctx.modules[moduleName] = modp;
        ctx.design.archModules[selection] = modp;
    }
}

//! Order entities so an entity follows the ones its architectures instantiate
void orderEntities(EmitContext& ctx) {
    std::map<std::string, const vhdlnative::EntityUnit*> entityByName;
    std::map<std::string, std::set<std::string>> instantiatedBy;
    for (const vhdlnative::EntityUnit& entity : ctx.library.entities) {
        entityByName.emplace(entity.name, &entity);
    }
    // Every architecture of an entity contributes dependencies: an instance may select one
    // that the default architecture does not use
    for (const vhdlnative::Architecture& arch : ctx.library.archs) {
        if (entityByName.count(arch.entityName)) {
            collectInstances(arch.stmts, instantiatedBy[arch.entityName]);
        }
    }
    {
        std::set<std::string> done;
        std::set<std::string> visiting;
        for (const vhdlnative::EntityUnit& entity : ctx.library.entities) {
            if (entityByName[entity.name] != &entity) {
                ctx.order.push_back(&entity);  // A second entity of this name; reported below
                continue;
            }
            if (!done.count(entity.name)) {
                orderVisit(entity.name, entityByName, instantiatedBy, done, visiting, ctx.order);
            }
        }
    }
}

//! Elaborate every entity in dependency order, recording the ones needing an instance
void elaborateEntities(EmitContext& ctx) {
    for (const vhdlnative::EntityUnit* const entryp : ctx.order) {
        const vhdlnative::EntityUnit& entity = *entryp;
        if (!ctx.entityNames.insert(entity.name).second) {
            entity.flp->v3error("Duplicate VHDL entity: '" + entity.spelling + "'.");
            continue;
        }
        const auto it = ctx.archByEntity.find(entity.name);
        if (it == ctx.archByEntity.end()) {
            entity.flp->v3error("Entity '" + entity.spelling
                                + "' has no architecture; cannot elaborate it.");
            continue;
        }
        if (vhdlnative::hasUnconstrainedPorts(entity, ctx.design.unconstrainedRecordMembers)) {
            // Only an instance's actuals fix the port ranges
            ctx.portUnelaborated.push_back(&entity);
            continue;
        }
        if (!vhdlnative::lower(ctx.m_rootp, entity, *it->second, ctx.modules[entity.name],
                               ctx.packages, ctx.modules, ctx.design)) {
            ctx.unelaborated.push_back(&entity);
            continue;
        }
        // Lower every additional architecture an instance selects, with the same generic
        // defaults the base module used
        for (const auto& entry : ctx.design.archModules) {
            if (entry.first.first != entity.name) continue;
            const auto byName = ctx.design.archsByName.find(entity.name);
            if (byName == ctx.design.archsByName.end()) continue;
            const auto archIt = byName->second.find(entry.first.second);
            if (archIt == byName->second.end()) continue;
            vhdlnative::lower(ctx.m_rootp, entity, *archIt->second, entry.second, ctx.packages,
                              ctx.modules, ctx.design);
        }
    }
}

//! Report unelaborated entities and remove the base modules no instance binds to
void finishElaboration(EmitContext& ctx) {
    for (const vhdlnative::Architecture& arch : ctx.library.archs) {
        if (!ctx.entityNames.count(arch.entityName)) {
            arch.flp->v3error("VHDL architecture does not name a declared entity.");
        }
    }
    // An entity whose generics have no defaults elaborates only through instances
    for (const vhdlnative::EntityUnit* const entityp : ctx.unelaborated) {
        const bool covered = vhdlnative::specializationCovers(ctx.design, entityp->name, false);
        if (covered) {
            // Only the specializations are referenced; the empty base would look like a top
            ctx.deletedModules.insert(entityp->name);
        } else if (ctx.design.instantiated.count(entityp->name)) {
            entityp->flp->v3error("VHDL: entity '" + entityp->spelling
                                  + "' has a generic without a default value, and no instance "
                                    "binds it.");
        } else if (ctx.modules[entityp->name]) {
            // An entity no instance names is not part of the design: nothing binds its
            // generics, and an empty module would look like another top
            ctx.deletedModules.insert(entityp->name);
        }
    }
    // An entity with unconstrained ports elaborates only through its instances
    for (const vhdlnative::EntityUnit* const entityp : ctx.portUnelaborated) {
        const bool covered = vhdlnative::specializationCovers(ctx.design, entityp->name, true);
        if (covered) {
            ctx.deletedModules.insert(entityp->name);
        } else {
            vhdlnative::reportUnspecializedPorts(*entityp, ctx.design.unconstrainedRecordMembers);
        }
    }
    // A base module no instance binds to is dead: it has no parent and would look like
    // another top. An instance that binds nothing uses the base itself.
    for (const auto& entry : ctx.modules) {
        if (!ctx.design.instantiated.count(entry.first)) continue;
        if (ctx.design.baseUsed.count(entry.first)) continue;
        ctx.deletedModules.insert(entry.first);
    }
    for (const std::string& name : ctx.deletedModules) {
        if (AstModule* const modp = ctx.modules[name]) {
            ctx.modules[name] = nullptr;
            VL_DO_DANGLING(modp->unlinkFrBack()->deleteTree(), modp);
        }
    }
}

}  // namespace

void V3VhdlNative::emit(AstNetlist* rootp, const std::vector<V3VhdlNativeFile>& files) {
    const int errorsBefore = V3Error::errorCount();
    EmitContext ctx{rootp};
    for (const V3VhdlNativeFile& file : files) vhdlnative::parseFile(file, ctx.library);
    // A parse error may have dropped design units, so elaboration would report spurious
    // missing-architecture and unknown-entity errors on top of the real diagnostic
    if (V3Error::errorCount() != errorsBefore) return;
    expandContexts(ctx.library);

    ctx.archByEntity = resolveArchitectures(ctx.library);
    prepareModules(ctx);
    createSelectedArchModules(ctx);
    orderEntities(ctx);
    elaborateEntities(ctx);
    finishElaboration(ctx);
}
