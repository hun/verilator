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

#include <map>
#include <memory>
#include <string>
#include <vector>

class AstModule;
class AstNetlist;

namespace vhdlnative {

//! Module created for each entity, so instances can bind to it by name
using ModuleMap = std::map<std::string, AstModule*>;

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

//! Elaborate the declarative parts of all packages
void lowerPackages(const std::vector<PackageUnit>& packages, AstNetlist* rootp,
                   PackageTable& table) VL_MT_DISABLED;

//! Create the module of every entity, so an instance can bind before it is lowered
void createModules(const std::vector<EntityUnit>& entities, AstNetlist* rootp,
                   ModuleMap& modules) VL_MT_DISABLED;

//! Lower one entity/architecture pair into the module created for its entity
void lower(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
           const PackageTable& table, const ModuleMap& modules) VL_MT_DISABLED;

}  // namespace vhdlnative

#endif  // Guard
