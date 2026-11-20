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
// SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************

#ifndef VERILATOR_V3VHDLNATIVE_LOWER_H_
#define VERILATOR_V3VHDLNATIVE_LOWER_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3VhdlNativeAst.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class AstModule;
class AstNetlist;

namespace vhdlnative {

//! Module created for each entity, so instances can bind to it by name
using ModuleMap = std::map<std::string, AstModule*>;

//! Index range fixed for an unconstrained entity port by one instantiation
struct PortBounds final {
    int left = 0;
    int right = 0;
    bool ascending = false;
    // A port of an unconstrained record: one member entry per element, declaration order
    bool isRecord = false;
    std::vector<std::string> memberNames;
    std::vector<int> memberLeft;
    std::vector<int> memberRight;
    std::vector<bool> memberAscending;
};
using PortBoundsMap = std::map<std::string, PortBounds>;

//! Record type mark -> the names of its unconstrained array elements
using UnconstrainedRecordMap = std::map<std::string, std::set<std::string>>;

//! Elaborated package declarations, shared by every unit that uses them
class PackageTable final {
public:
    struct Impl;  // Elaborated packages, private to the lowering stage

    PackageTable();
    ~PackageTable();
    PackageTable(const PackageTable&) = delete;
    PackageTable& operator=(const PackageTable&) = delete;

    Impl& impl() { return *m_implp; }
    const Impl& impl() const { return *m_implp; }

private:
    std::unique_ptr<Impl> m_implp;
};

struct Design;

//! Elaborate the declarative parts of all packages
void lowerPackages(const DesignLibrary& library, AstNetlist* rootp, PackageTable& table,
                   Design& design) VL_MT_DISABLED;

//! Create the module of every entity, so an instance can bind before it is lowered
void createModules(const std::vector<EntityUnit>& entities, AstNetlist* rootp,
                   ModuleMap& modules) VL_MT_DISABLED;

//! Design context shared by every lowering of one run. Entities whose generics lack
//! a default get a module per generic binding; the specializations are cached by key.
struct Design final {
    std::map<std::string, const EntityUnit*> entities;  // Folded entity name -> entity
    // Folded entity name -> default architecture
    std::map<std::string, const Architecture*> archs;
    // Folded entity name -> folded architecture name -> architecture
    std::map<std::string, std::map<std::string, const Architecture*>> archsByName;
    // (entity, architecture) selected by an instance -> the module lowered with it
    std::map<std::pair<std::string, std::string>, AstModule*> archModules;
    std::map<std::string, AstModule*> specializations;  // Generic binding key -> module
    std::set<std::string> instantiated;  // Entities named by at least one instance
    std::set<std::string> baseUsed;  // Entities whose base module an instance binds to
    UnconstrainedRecordMap unconstrainedRecordMembers;  // Unconstrained record elements
    // Physical unit name -> multiple of its type's step unit, and the names two physical
    // types give different multiples to, which no literal can name without its type
    std::map<std::string, int64_t> unitFactors;
    std::set<std::string> ambiguousUnits;
    int specSeq = 0;  // Disambiguates the specialized module names
    int specDepth = 0;  // Generic specialization recursion depth
};

//! Whether a port spec fixes every unconstrained element of its record type
bool recordPortFullyConstrained(const PortDecl& port, const UnconstrainedRecordMap& members);

//! Whether an entity port's index range is fixed by each instance, not the entity
bool hasUnconstrainedPorts(const EntityUnit& entity, const UnconstrainedRecordMap& members);

//! Whether an entity's base module needs an instance's generic or port bindings
bool entityNeedsSpecialization(const EntityUnit& entity, const Design& design);

//! Report why an unconstrained-port entity with no instance cannot be modelled
void reportUnspecializedPorts(const EntityUnit& entity, const UnconstrainedRecordMap& members);

//! Record the unconstrained elements of every record type declared in the library
void collectUnconstrainedRecordMembers(const DesignLibrary& library,
                                       UnconstrainedRecordMap& out) VL_MT_DISABLED;

//! Record the physical unit names declared by the packages and entities of the library
void collectPhysicalUnits(const DesignLibrary& library, Design& design) VL_MT_DISABLED;

//! Lower one entity/architecture pair into the module created for its entity. False
//! if the entity's generics have no defaults to elaborate, and no binding supplied
bool lower(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
           const PackageTable& table, const ModuleMap& modules, Design& design,
           const PortBoundsMap* portBounds = nullptr) VL_MT_DISABLED;

}  // namespace vhdlnative

#endif  // Guard
