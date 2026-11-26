// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: lowering to the AST
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
// Lowers one analyzed entity/architecture pair into the committed pre-width
// Verilator AST: the node shapes, dtype canonicalization and re-entry contract
// match what V3VhdlLink expects. VHDL identifiers are case-insensitive, so a name
// is emitted under its folded (lowercase) form -- the spelling as written stays
// available for diagnostics only.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNativeLower.h"

#include "V3Error.h"
#include "V3FileLine.h"
#include "V3VhdlNativeLowerImpl.h"
#include "V3VhdlNativeTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vhdlnative {

//! Whether two base type names are the same VHDL type or closely related (IEEE
//! 1076-2008 9.3.6): std_logic is the resolved subtype of std_ulogic, and
//! std_logic_vector of std_ulogic_vector (IEEE 1164)
int controlCharacterCode(const std::string& name) {
    static const char* const names[]
        = {"nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs",  "ht",  "lf",
           "vt",  "ff",  "cr",  "so",  "si",  "dle", "dc1", "dc2", "dc3", "dc4", "nak",
           "syn", "etb", "can", "em",  "sub", "esc", "fsp", "gsp", "rsp", "usp"};
    for (int code = 0; code < 32; ++code) {
        if (name == names[code]) return code;
    }
    return name == "del" ? 127 : -1;
}

bool baseTypesRelated(const std::string& a, const std::string& b) {
    if (a == b) return true;
    const auto logic
        = [](const std::string& name) { return name == "std_logic" || name == "std_ulogic"; };
    const auto logicVector = [](const std::string& name) {
        return name == "std_logic_vector" || name == "std_ulogic_vector";
    };
    return (logic(a) && logic(b)) || (logicVector(a) && logicVector(b));
}

//! The base type an array operator sees. The resolved VHDL-2008 numeric types are
//! subtypes of the numeric_std ones, so `u_unsigned` and `unsigned` are one base type.
bool arrayBaseTypesRelated(const std::string& a, const std::string& b) {
    const auto canonical = [](std::string name) {
        if (name == "u_unsigned") return std::string{"unsigned"};
        if (name == "u_signed") return std::string{"signed"};
        return name;
    };
    return baseTypesRelated(canonical(a), canonical(b));
}

//! Whether an integer expression is built only from literals and static scalar attributes,
//! so evaluating it cannot depend on a folded constant this pass has not completed
bool literalIntegerExpr(const Expr& expr) {
    switch (expr.kind) {
    case Expr::INT_LIT: return true;
    case Expr::PAREN:
    case Expr::UNARY: return expr.a && literalIntegerExpr(*expr.a);
    case Expr::BINARY:
        return expr.a && expr.b && literalIntegerExpr(*expr.a) && literalIntegerExpr(*expr.b);
    case Expr::ATTR:
        return expr.a && expr.a->kind == Expr::NAME
               && (expr.text == "high" || expr.text == "low" || expr.text == "left"
                   || expr.text == "right" || expr.text == "length");
    default: return false;
    }
}

//! Whether a case or selected-assignment selector is discrete, or a one-dimensional
//! array of a discrete element type (IEEE 1076-2008 10.9)
bool discreteSelectorType(const VType& type) {
    if (type.kind == VType::LOGIC || type.kind == VType::BIT || type.kind == VType::BOOLEAN
        || type.kind == VType::INT || type.kind == VType::ENUM) {
        return true;
    }
    if (type.kind == VType::VECTOR) return true;
    if (type.kind == VType::ARRAY && type.elemp) {
        return type.elemp->kind == VType::LOGIC || type.elemp->kind == VType::BIT
               || type.elemp->kind == VType::BOOLEAN || type.elemp->kind == VType::ENUM;
    }
    return false;
}

//! Whether a character literal may adopt `target`'s type (IEEE 1076-2008 9.3.2, 15.5)
bool charLiteralFits(const VType& target, char ch) {
    if (target.kind == VType::INT || target.kind == VType::REAL || target.kind == VType::FILE
        || target.kind == VType::LINE) {
        return false;
    }
    if (target.kind == VType::LOGIC) return target.typeName != "boolean";
    if (target.kind == VType::BIT) return true;
    if (target.kind == VType::ENUM) {
        const std::string key = std::string{"'"} + ch + "'";
        return std::find(target.literals.begin(), target.literals.end(), key)
               != target.literals.end();
    }
    // A character is modelled as its eight-bit code
    return target.isVector() && target.width == 8 && target.typeName == "character";
}

//! Elaborated contents of one package, publishable to the units that use it
//! An alias: the designator it renames, and the type its mark declares

PackageTable::PackageTable()
    : m_implp{new Impl} {}
PackageTable::~PackageTable() = default;

//! The multiple of the step unit that a predefined unit of `time' stands for, or false for
//! any other name. A physical type's own unit of the same name shadows the predefined one.
bool predefinedUnit(const std::string& unit, int64_t& out) {
    // The step unit of `time' is the femtosecond, so 2 sec is 2e15 and needs 64 bits
    static const std::pair<const char*, int64_t> timeUnits[] = {{"fs", 1},
                                                                {"fsec", 1},
                                                                {"ps", 1000},
                                                                {"psec", 1000},
                                                                {"ns", 1000000},
                                                                {"nsec", 1000000},
                                                                {"us", 1000000000},
                                                                {"usec", 1000000000},
                                                                {"ms", 1000000000000},
                                                                {"msec", 1000000000000},
                                                                {"sec", 1000000000000000},
                                                                {"min", 60000000000000000},
                                                                {"hr", 3600000000000000000}};
    for (const std::pair<const char*, int64_t>& timeUnit : timeUnits) {
        if (unit == timeUnit.first) {
            out = timeUnit.second;
            return true;
        }
    }
    return false;
}

//! Value of a physical literal: the abstract literal scaled to its type's step unit. The
//! literal names its unit but not its type, so the name is looked up in the table every
//! physical type's declaration adds to, which spans the design.
bool physicalValue(const Design& design, const Expr& expr, int64_t& out) {
    if (design.ambiguousUnits.count(expr.text)) {
        unsupportedError(expr.flp, "physical unit '" + expr.spelling + "' of two types");
        return false;
    }
    int64_t factor = 1;  // An abstract literal written without a unit names the step unit
    if (!expr.text.empty()) {
        const auto it = design.unitFactors.find(expr.text);
        if (it != design.unitFactors.end()) {
            factor = it->second;
        } else if (!predefinedUnit(expr.text, factor)) {
            expr.flp->v3error("Unknown VHDL physical unit: '" + expr.spelling + "'.");
            return false;
        }
    }
    out = std::llround(expr.realValue * static_cast<double>(factor));
    return true;
}

//! Record one physical type's unit names and their multiples of the step unit. The first
//! declaration sets the step; each later one names an earlier unit of any physical type, or
//! a predefined one.
void addPhysicalUnits(const TypeDecl& decl, Design& design) {
    int64_t factor = 1;
    for (const TypeDecl::Unit& unit : decl.units) {
        if (unit.value && !physicalValue(design, *unit.value, factor)) return;
        const auto seen = design.unitFactors.find(unit.name);
        // Two physical types that are not each other's scope may give one name a different
        // multiple; only the expected type tells a literal which one it means
        if (seen != design.unitFactors.end() && seen->second != factor) {
            design.ambiguousUnits.insert(unit.name);
        }
        design.unitFactors[unit.name] = factor;
    }
}

//! Whether the expression converts to a real somewhere: such a conversion has no form
//! in the model, so the expression it belongs to must fold at elaboration time
bool mentionsRealValue(const Expr& expr) {
    if (expr.kind == Expr::REAL_LIT) return true;
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "real" || isMathRealFunction(expr.a->text))) {
        return true;
    }
    if (expr.a && mentionsRealValue(*expr.a)) return true;
    if (expr.b && mentionsRealValue(*expr.b)) return true;
    for (const ExprP& argp : expr.args) {
        if (argp && mentionsRealValue(*argp)) return true;
    }
    return false;
}

void lowerPackages(const DesignLibrary& library, AstNetlist* rootp, PackageTable& table,
                   Design& design) {
    // The table under construction is also what a package may import: a package's `use`
    // clause names the packages elaborated before it, as for an entity or architecture
    Lowering lowering{rootp, &table.impl(), nullptr, &design};
    // A package that declares generics is a template: elaborate only its instances.
    const auto isTemplate = [](const PackageUnit& pkg) { return !pkg.generics.empty(); };
    // Libraries are not a namespace here, so two units of one name would silently
    // replace each other in the table; report the second, as for a duplicate entity
    std::set<std::string> specNames;
    std::set<std::string> bodyNames;
    lowering.scanBodyIntConsts(library.packages);
    for (const PackageUnit& pkg : library.packages) {
        std::set<std::string>& names = pkg.isBody ? bodyNames : specNames;
        if (!names.insert(pkg.name).second) {
            pkg.flp->v3error(
                (pkg.isBody ? "Duplicate VHDL package body: '" : "Duplicate VHDL package: '")
                + pkg.spelling + "'.");
            continue;
        }
        if (isTemplate(pkg)) continue;
        lowering.emitPackage(pkg, table.impl());
    }
    for (const PackageInstance& inst : library.packageInstances) {
        if (!specNames.insert(inst.name).second) {
            inst.flp->v3error("Duplicate VHDL package: '" + inst.spelling + "'.");
            continue;
        }
        lowering.instantiatePackage(library.packages, inst, table.impl());
    }
    // A body completes a specification; without one it has no declarative region to extend
    for (const PackageUnit& pkg : library.packages) {
        if (pkg.isBody && !specNames.count(pkg.name)) {
            pkg.flp->v3error("VHDL: package body '" + pkg.spelling
                             + "' has no package specification.");
        }
    }
    // A specification constant whose call had no body when the specification was emitted
    // is completed by a second pass over the specifications, now that every body is read
    for (int pass = 0; pass < 4 && lowering.hasDeferredConsts(); ++pass) {
        const std::string owner = lowering.deferredOwner();
        const auto it = std::find_if(
            library.packages.begin(), library.packages.end(),
            [&](const PackageUnit& pkg) { return !pkg.isBody && pkg.name == owner; });
        if (it == library.packages.end()) break;
        lowering.emitPackage(*it, table.impl());
    }
}

void linkPackageSignalRefs(AstNetlist* rootp, const PackageTable& table) {
    // A reference to a package signal names its package, which the scope pass needs to find
    // the variable's scope; the references of one unit cannot name it themselves
    std::unordered_map<const AstVar*, AstPackage*> owners;
    for (const auto& entry : table.impl().packages) {
        for (const auto& signal : entry.second.signals) {
            owners[signal.second.varp] = signal.second.pkgp;
        }
    }
    if (owners.empty()) return;
    for (AstNodeModule* const modp : rootp->modulespList()) {
        modp->foreach([&](AstVarRef* const refp) {
            const auto it = owners.find(refp->varp());
            if (it != owners.end()) refp->classOrPackagep(it->second);
        });
    }
}

void createModules(const std::vector<EntityUnit>& entities, AstNetlist* rootp,
                   ModuleMap& modules) {
    for (const EntityUnit& entity : entities) {
        if (modules.count(entity.name)) continue;
        AstModule* const modp = new AstModule{entity.flp, entity.name, entity.libname};
        modp->inLibrary(entity.inLibrary);
        rootp->addModulesp(modp);
        modules[entity.name] = modp;
    }
}

//! Whether a record element's type is an unconstrained array (its width is set elsewhere)
bool memberTypeUnconstrained(const TypeSpec& type) {
    const std::string& mark = type.mark;
    const bool unconstrainedMark
        = mark == "std_logic_vector" || mark == "std_ulogic_vector" || mark == "bit_vector"
          || mark == "unsigned" || mark == "signed" || mark == "u_unsigned" || mark == "u_signed";
    return unconstrainedMark && !type.hasRange && type.constraintMark.empty();
}

void collectUnconstrainedRecordMembers(const DesignLibrary& library, UnconstrainedRecordMap& out) {
    const auto scan = [&](const std::vector<TypeDecl>& types) {
        for (const TypeDecl& decl : types) {
            if (decl.kind != TypeDecl::RECORD) continue;
            std::unordered_set<std::string> members;
            for (const TypeDecl::Member& member : decl.members) {
                if (!memberTypeUnconstrained(member.type)) continue;
                for (const auto& name : member.names) members.insert(name.first);
            }
            if (!members.empty()) out[decl.name] = std::move(members);
        }
    };
    for (const PackageUnit& pkg : library.packages) scan(pkg.types);
    for (const EntityUnit& entity : library.entities) scan(entity.types);
}

//! Record the physical unit names of every package and entity, each with the multiple of
//! its type's step unit. A generic default is lowered before the declarations of the unit
//! that owns it, so a literal's unit has to be findable among types not elaborated yet.
void collectPhysicalUnits(const DesignLibrary& library, Design& design) {
    const auto scan = [&design](const std::vector<TypeDecl>& types) {
        for (const TypeDecl& decl : types) {
            if (decl.kind == TypeDecl::PHYSICAL) addPhysicalUnits(decl, design);
        }
    };
    for (const PackageUnit& pkg : library.packages) scan(pkg.types);
    for (const EntityUnit& entity : library.entities) scan(entity.types);
}

bool recordPortFullyConstrained(const PortDecl& port, const UnconstrainedRecordMap& members) {
    const auto it = members.find(port.type.mark);
    if (it == members.end()) return false;
    for (const std::string& member : it->second) {
        const bool named
            = std::any_of(port.type.memberConstraints.begin(), port.type.memberConstraints.end(),
                          [&](const TypeSpec::MemberConstraint& constraint) {
                              return constraint.name == member;
                          });
        if (!named) return false;
    }
    return true;
}

bool hasUnconstrainedPorts(const EntityUnit& entity, const UnconstrainedRecordMap& members) {
    for (const PortDecl& port : entity.ports) {
        const std::string& mark = port.type.mark;
        const bool unconstrainedMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                                       || mark == "bit_vector" || mark == "unsigned"
                                       || mark == "signed";
        if (unconstrainedMark && !port.type.hasRange && port.type.constraintMark.empty())
            return true;
        if (!port.type.hasRange && port.type.constraintMark.empty() && members.count(mark)
            && !recordPortFullyConstrained(port, members))
            return true;
    }
    return false;
}

bool entityNeedsSpecialization(const EntityUnit& entity, const Design& design) {
    if (hasUnconstrainedPorts(entity, design.unconstrainedRecordMembers)) return true;
    for (const GenericDecl& gen : entity.generics) {
        // A type or subprogram generic has no default, and a value generic without one
        // cannot elaborate the base module
        if (gen.isType || gen.isSubprogram || !gen.init) return true;
    }
    return false;
}

//! Report why an unconstrained-port entity with no instance cannot be modelled, at the
//! offending port, matching the message a top-level use of the type would give
void reportUnspecializedPorts(const EntityUnit& entity, const UnconstrainedRecordMap& members) {
    for (const PortDecl& port : entity.ports) {
        const std::string& mark = port.type.mark;
        const bool unconstrainedMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                                       || mark == "bit_vector" || mark == "unsigned"
                                       || mark == "signed";
        if ((!port.type.hasRange && port.type.constraintMark.empty()
             && (unconstrainedMark
                 || (members.count(mark) && !recordPortFullyConstrained(port, members))))) {
            unsupportedError(port.flp, "unconstrained type '" + mark + "'");
            return;
        }
    }
}

bool lower(AstNetlist* rootp, const EntityUnit& entity, const Architecture& arch, AstModule* modp,
           const PackageTable& table, const ModuleMap& modules, Design& design,
           const PortBoundsMap* portBoundsp) {
    Lowering lowering{rootp, &table.impl(), &modules, &design};
    return lowering.emit(entity, arch, modp, nullptr, portBoundsp);
}

}  // namespace vhdlnative
