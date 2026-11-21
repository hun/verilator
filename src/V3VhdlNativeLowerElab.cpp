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
// Elaborates entity instances, generic specializations and port bindings
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3Error.h"
#include "V3FileLine.h"
#include "V3VhdlNativeLower.h"
#include "V3VhdlNativeLowerImpl.h"

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
#include <utility>
#include <vector>

namespace vhdlnative {

void Lowering::freeInstanceBindings() {
    for (auto& entry : m_instanceBindings) {
        if (entry.second.valuep) entry.second.valuep->deleteTree();
    }
    m_instanceBindings.clear();
}

bool Lowering::registerGenerics(const std::vector<GenericDecl>& generics,
                                const GenericBindings* bindingsp, bool probe) {
    bool ok = true;
    bool unbound = false;  // A generic here has neither a binding nor a default
    const auto registerString
        = [&](FileLine* flp, const std::string& folded, const std::string& text) {
              // The text is kept whatever its length: an empty string still carries it to
              // the instances it is passed to, and comparisons fold on it
              m_stringConsts[folded] = text;
              // A string generic's value is fixed at elaboration, so register it as a typed
              // constant of packed 8-bit character codes rather than a variable; that lets a
              // reference fold, e.g. `if val = "one" generate`. An empty string is the
              // null string `1 to 0`: it has no characters, but its bounds and length are
              // still values the design can read.
              const VType type = stringType(flp, static_cast<int>(text.size()));
              AstConst* const valuep = makeStringConst(flp, text);
              m_ownedConsts.push_back(std::unique_ptr<AstConst>(valuep));
              m_constValues[folded] = m_ownedConsts.back().get();
              m_constTypes[folded] = type;
          };
    for (const GenericDecl& gen : generics) {
        for (const auto& name : gen.names) {
            if (unbound) continue;  // An earlier generic has no value, so nothing can elaborate
            if (gen.isType) {
                if (bindingsp) {
                    const auto it = bindingsp->find(name.first);
                    if (it != bindingsp->end() && it->second.isType) {
                        if (!probe) {
                            m_types[name.first] = it->second.typeValue;
                            m_imported.insert(name.first);
                        }
                        continue;
                    }
                }
                if (!probe) {
                    ok = false;  // A type generic has no elaboratable default
                    unbound = true;
                }
                continue;
            }
            if (gen.isSubprogram) {
                if (bindingsp) {
                    const auto it = bindingsp->find(name.first);
                    if (it != bindingsp->end() && it->second.subp) {
                        if (!probe) {
                            m_subprograms[name.first].push_back(
                                SubprogramRef{it->second.subp, false, -1});
                            m_imported.erase(name.first);
                        }
                        continue;
                    }
                }
                if (!probe) {
                    ok = false;  // A subprogram generic has no elaboratable default
                    unbound = true;
                }
                continue;
            }
            if (gen.type.mark == "real") {
                if (bindingsp) {
                    const auto it = bindingsp->find(name.first);
                    if (it != bindingsp->end() && it->second.isReal) {
                        if (!probe) m_realConsts[name.first] = it->second.realValue;
                        continue;
                    }
                }
                double realDefault = 0.0;
                if (gen.init && evalReal(*gen.init, realDefault)) {
                    if (!probe) m_realConsts[name.first] = realDefault;
                } else if (!probe) {
                    ok = false;  // No default, so only a binding can elaborate the base
                    unbound = true;
                }
                continue;
            }
            if (gen.type.mark == "string") {
                if (bindingsp) {
                    const auto it = bindingsp->find(name.first);
                    if (it != bindingsp->end() && it->second.isString) {
                        if (!probe) registerString(gen.flp, name.first, it->second.stringValue);
                        continue;
                    }
                }
                // A default may also be an empty or logic-character string, which parses
                // as a bit-string literal, or a constant that holds one
                std::string defaultText;
                if (gen.init && stringOperandText(*gen.init, defaultText)) {
                    if (!probe) registerString(gen.flp, name.first, defaultText);
                } else if (!probe) {
                    ok = false;  // No default, so only a binding can elaborate the base
                    unbound = true;
                }
                continue;
            }
            if (bindingsp) {
                const auto it = bindingsp->find(name.first);
                if (it != bindingsp->end()) {
                    if (it->second.isInt) {
                        m_consts[name.first] = it->second.intValue;
                        VType declared;
                        if (resolveType(gen.type, declared)) m_constTypes[name.first] = declared;
                    } else {
                        m_ownedConsts.push_back(
                            std::unique_ptr<AstConst>(it->second.valuep->cloneTree(false)));
                        m_constValues[name.first] = m_ownedConsts.back().get();
                        if (it->second.hasValueType) {
                            // The declared type, with the bounds this instance's actual gave
                            m_constTypes[name.first] = it->second.valueType;
                        } else if (unconstrainedVectorSpec(gen.type)) {
                            m_constTypes[name.first] = typeOfDType(it->second.valuep->dtypep());
                        } else {
                            VType declared;
                            if (resolveType(gen.type, declared)) {
                                m_constTypes[name.first] = declared;
                            }
                        }
                    }
                    continue;
                }
            }
            if (!gen.init) {
                ok = false;
                unbound = true;
                continue;
            }
            if (probe) {
                int64_t value = 0;
                if (evalInt(*gen.init, value)) m_consts[name.first] = value;
                continue;
            }
            emitConstant({name}, gen.type, gen.init.get(), gen.flp, false);
        }
    }
    return ok;
}

void Lowering::importPackage(const std::string& name, const std::string& only) {
    if (only.empty()) {
        if (name == "std_logic_signed") m_stdLogicSigned = true;
        if (name == "std_logic_unsigned") m_stdLogicUnsigned = true;
        if (name == "std_logic_arith") m_stdLogicArith = true;
        if (name == "numeric_std_unsigned") m_numericStdUnsigned = true;
        if (name == "numeric_std_signed") m_numericStdSigned = true;
    }
    if (!m_packagesp) return;
    const auto pkgIt = m_packagesp->packages.find(name);
    if (pkgIt == m_packagesp->packages.end()) return;  // Standard package: intrinsics
    const PackageInfo& info = pkgIt->second;
    // A single-declaration use makes only the declaration it names visible
    const auto wants = [&](const std::string& key) { return only.empty() || key == only; };
    for (const auto& alias : info.aliases) {
        if (!wants(alias.first)) continue;
        if (m_aliases.count(alias.first)) continue;
        m_aliases[alias.first] = alias.second;
    }

    const bool ownSpec = !m_unitName.empty() && name == m_unitName;
    for (const auto& type : info.types) {
        if (!wants(type.first)) continue;
        if (ownSpec) {
            // A package body sees its own specification closer than any imported package
            m_types[type.first] = type.second;
            m_ambiguousTypes.erase(type.first);
            continue;
        }
        const auto existing = m_types.find(type.first);
        if (existing != m_types.end()) {
            if (!existing->second.typeName.empty() && !type.second.typeName.empty()
                && existing->second.typeName != type.second.typeName) {
                m_ambiguousTypes.insert(type.first);
            }
            continue;
        }
        m_types[type.first] = type.second;
        m_imported.insert(type.first);
    }
    for (const auto& constant : info.consts) {
        if (!wants(constant.first)) continue;
        if (m_consts.count(constant.first) || m_constValues.count(constant.first)) continue;
        m_consts[constant.first] = constant.second;
        m_imported.insert(constant.first);
    }
    for (const auto& value : info.constValues) {
        if (!wants(value.first)) continue;
        if (m_consts.count(value.first) || m_constValues.count(value.first)) continue;
        m_ownedConsts.push_back(std::unique_ptr<AstConst>(value.second->cloneTree(false)));
        m_constValues[value.first] = m_ownedConsts.back().get();
        m_imported.insert(value.first);
    }
    for (const auto& type : info.constTypes) {
        if (!wants(type.first)) continue;
        if (m_constTypes.count(type.first)) continue;
        m_constTypes[type.first] = type.second;
    }
    for (const auto& realConst : info.realConsts) {
        if (!wants(realConst.first)) continue;
        if (m_realConsts.count(realConst.first)) continue;
        m_realConsts[realConst.first] = realConst.second;
        m_imported.insert(realConst.first);
    }
    if (only.empty()) {
        m_ambiguousLiterals.insert(info.ambiguousLiterals.begin(), info.ambiguousLiterals.end());
    }
    for (const auto& literal : info.enumConsts) {
        if (!wants(literal.first)) continue;
        const auto existing = m_enumConsts.find(literal.first);
        if (existing != m_enumConsts.end()) {
            if (existing->second->dtypep() != literal.second->dtypep()) {
                m_ambiguousLiterals.insert(literal.first);
            }
            continue;
        }
        m_ownedConsts.push_back(std::unique_ptr<AstConst>(literal.second->cloneTree(false)));
        m_enumConsts[literal.first] = m_ownedConsts.back().get();
        m_imported.insert(literal.first);
    }
    for (const auto& component : info.components) {
        if (!wants(component.first)) continue;
        if (m_components.count(component.first)) continue;  // A local declaration wins
        m_components[component.first] = component.second;
        m_imported.insert(component.first);
    }
    for (const SubprogramDecl* const subp : info.subprograms) {
        if (!wants(subp->name)) continue;
        std::vector<SubprogramRef>& refs = m_subprograms[subp->name];
        const bool known = std::any_of(refs.begin(), refs.end(),
                                       [=](const SubprogramRef& r) { return r.declp == subp; });
        if (known) continue;
        // A declaration this unit makes itself hides an imported homograph, so importing
        // it again must not undo that; the unit's own specification is the declaration
        // its body completes (IEEE 1076-2008 12.4)
        if (!ownSpec && std::any_of(refs.begin(), refs.end(), [&](const SubprogramRef& ref) {
                return !ref.imported && homograph(*ref.declp, *subp);
            })) {
            continue;
        }
        refs.push_back(SubprogramRef{subp, false, -1, true, ownSpec});
        m_imported.insert(subp->name);
    }
    // A synonym is resolved here, where the subprogram it designates is visible too
    for (const AliasDecl* const aliasp : info.subprogramAliases) {
        if (!wants(aliasp->name)) continue;
        registerSubprogramAlias(*aliasp);
    }
}

//! Everything a package's body sees when it is inlined: the interface, the packages its
//! `use` clauses named, and the subprograms only its body declares. A declaration of the
//! body hides an imported homograph the caller had in scope (IEEE 1076-2008 12.4)
void Lowering::importInlineScope(const std::string& name) {
    importPackage(name);
    if (!m_packagesp) return;
    const auto pkgIt = m_packagesp->packages.find(name);
    if (pkgIt == m_packagesp->packages.end()) return;
    for (const auto& use : pkgIt->second.uses) importPackage(use.first, use.second);
    // The unit's `use` clauses are visible in its body, but its own declarations hide
    // the homographs they bring in (IEEE 1076-2008 12.4), so they become local again
    const auto declareOwn = [&](const SubprogramDecl* subp) {
        std::vector<SubprogramRef>& refs = m_subprograms[subp->name];
        refs.erase(std::remove_if(refs.begin(), refs.end(),
                                  [&](const SubprogramRef& ref) {
                                      return ref.imported && homograph(*ref.declp, *subp);
                                  }),
                   refs.end());
        const bool known = std::any_of(refs.begin(), refs.end(),
                                       [=](const SubprogramRef& r) { return r.declp == subp; });
        if (!known) refs.push_back(SubprogramRef{subp, false, -1});
    };
    for (const SubprogramDecl* const subp : pkgIt->second.subprograms) declareOwn(subp);
    for (const SubprogramDecl* const subp : pkgIt->second.bodySubprograms) declareOwn(subp);
}

//! Add what one more emission of a package publishes to its existing entry
static void mergePackageInfo(PackageInfo& dest, PackageInfo& info) {
    for (const auto& alias : info.aliases) dest.aliases[alias.first] = alias.second;
    for (const auto& type : info.types) dest.types[type.first] = type.second;
    for (const auto& constant : info.consts) dest.consts[constant.first] = constant.second;
    for (const auto& value : info.constValues) dest.constValues[value.first] = value.second;
    for (const auto& type : info.constTypes) dest.constTypes[type.first] = type.second;
    for (const auto& literal : info.enumConsts) dest.enumConsts[literal.first] = literal.second;
    for (const auto& realConst : info.realConsts) {
        dest.realConsts[realConst.first] = realConst.second;
    }
    for (std::unique_ptr<AstConst>& constp : info.owned) dest.owned.push_back(std::move(constp));
    for (const SubprogramDecl* const subp : info.subprograms) dest.subprograms.push_back(subp);
    for (const auto& component : info.components) {
        dest.components[component.first] = component.second;
    }
    for (const SubprogramDecl* const subp : info.bodySubprograms) {
        if (std::find(dest.bodySubprograms.begin(), dest.bodySubprograms.end(), subp)
            == dest.bodySubprograms.end()) {
            dest.bodySubprograms.push_back(subp);
        }
    }
    for (const auto& use : info.uses) {
        if (std::find(dest.uses.begin(), dest.uses.end(), use) == dest.uses.end()) {
            dest.uses.push_back(use);
        }
    }
    for (const AliasDecl* const aliasp : info.subprogramAliases) {
        dest.subprogramAliases.push_back(aliasp);
    }
}

//! Complete the constants this unit deferred because a call had no body yet. They run
//! after the unit's own constants, because the call may name one of them, and only the
//! unit that declared one completes it: the scope of another unit is not its own
void Lowering::completeDeferredConsts() {
    std::vector<DeferredConst> owned;
    std::vector<DeferredConst> others;
    for (const DeferredConst& deferred : m_deferredConsts) {
        (deferred.owner == m_unitName ? owned : others).push_back(deferred);
    }
    m_deferredConsts = std::move(others);
    for (const DeferredConst& deferred : owned) {
        // A later pass may already have emitted it now that the bodies are read
        bool present = false;
        for (const auto& name : deferred.names) {
            if (m_consts.count(name.first) || m_constValues.count(name.first)) present = true;
        }
        if (present) continue;
        emitConstant(deferred.names, *deferred.typep, deferred.initp, deferred.flp, true);
    }
    // A type that names a constant this unit could not complete waits for the pass that
    // does complete it, rather than reporting the name as unknown
    const bool stillDeferred
        = std::any_of(m_deferredConsts.begin(), m_deferredConsts.end(),
                      [&](const DeferredConst& d) { return d.owner == m_unitName; });
    if (!stillDeferred) finishDeferredTypes();
}

//! What a package publishes, taken from the tables its emission filled; clears them
PackageInfo Lowering::publishedInfo(const PackageUnit& pkg) {
    PackageInfo info;
    info.aliases = m_aliases;
    info.types = std::move(m_types);
    info.consts = std::move(m_consts);
    info.constValues = std::move(m_constValues);
    info.constTypes = std::move(m_constTypes);
    info.enumConsts = std::move(m_enumConsts);
    info.ambiguousLiterals = m_ambiguousLiterals;
    info.realConsts = std::move(m_realConsts);
    info.owned = std::move(m_ownedConsts);
    for (const ComponentDecl& component : pkg.components) {
        info.components[component.name] = &component;
    }
    // Only what the unit declares publishes: a declaration its `use` clauses made
    // visible is not part of its interface (IEEE 1076-2008 12.4). A declaration only the
    // body makes is private to it, unless it completes one the specification published;
    // poc's configuration declares `ite` in its body, and open-logic's `ppcOr` calls
    // `log2ceil` of the math package, which is inlined with that package's scope
    for (const auto& entry : m_subprograms) {
        for (const SubprogramRef& ref : entry.second) {
            if (ref.imported) continue;
            const bool completesSpec = std::any_of(
                entry.second.begin(), entry.second.end(), [&](const SubprogramRef& other) {
                    return other.ownSpec && homograph(*other.declp, *ref.declp);
                });
            if (pkg.isBody && !completesSpec) {
                info.bodySubprograms.push_back(ref.declp);
                continue;
            }
            if (std::find(info.subprograms.begin(), info.subprograms.end(), ref.declp)
                == info.subprograms.end()) {
                info.subprograms.push_back(ref.declp);
            }
        }
    }
    for (const auto& use : pkg.uses) {
        const std::pair<std::string, std::string> entry{use.pkg, use.decl};
        if (std::find(info.uses.begin(), info.uses.end(), entry) == info.uses.end()) {
            info.uses.push_back(entry);
        }
    }
    for (const AliasDecl& alias : pkg.aliases) {
        if (alias.hasSignature) info.subprogramAliases.push_back(&alias);
    }
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_realConsts.clear();
    m_preConsts.clear();
    m_ownedConsts.clear();
    m_aliases.clear();
    return info;
}

void Lowering::emitPackage(const PackageUnit& pkg, PackageTable::Impl& table,
                           const GenericBindings* bindingsp, const std::string& publishName) {
    const std::string& pubName = publishName.empty() ? pkg.name : publishName;
    m_unitName = pubName;

    // A package sees its own declarations and the packages its `use` clauses name
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_preConsts.clear();
    m_ownedConsts.clear();
    m_imported.clear();
    m_ambiguousTypes.clear();
    m_arrayDtypes.clear();
    m_stdLogicSigned = false;
    m_stdLogicUnsigned = false;
    m_stdLogicArith = false;
    m_scope.clear();
    m_aliases.clear();
    m_subprograms.clear();
    m_components.clear();
    // The packages named by the unit's `use` clauses are visible here as well, as they
    // are in an entity or an architecture. What the package declares itself wins: those
    // registrations overwrite, and `importPackage` never replaces a name it finds.
    for (const auto& use : pkg.uses) importPackage(use.pkg, use.decl);
    // A body is nested in its specification, so what the specification declares, and what
    // its own `use` clauses made visible to it, is visible in the body too. The
    // specification is always published first: lowerPackages runs a pass over the
    // specifications before any body, and an instance emits its specification directly
    // before its body.
    if (pkg.isBody) importPackage(pubName);
    // A generic package is emitted once per instance, with its generics already bound
    if (bindingsp) registerGenerics(pkg.generics, bindingsp, false);
    // Integer constants are folded first so a type range (a record element, an array
    // bound, an integer subtype) that names one resolves during type registration,
    // exactly as an entity or architecture prescans before registering its types.
    preScanIntConstants(pkg.decls);
    registerTypes(pkg.types, pkg.decls);
    registerComponents(pkg.components);
    // Subprograms are visible to the declarative items below, including deferred
    // constants, and to an alias renaming one of them
    registerSubprograms(pkg.subprograms, -1);
    for (const AliasDecl& alias : pkg.aliases) registerAlias(alias);
    // Preliminary snapshot: a constant's value may already call a subprogram, whose body
    // evaluates against this scope and not against whatever the caller has
    m_moduleScope = saveScope();
    for (const Decl& decl : pkg.decls) {
        emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, !pkg.isBody);
        // A subprogram of this unit evaluates against what the unit has declared before
        // it, so a body that reads a constant declared earlier in the same package body
        // sees it (IEEE 1076-2008 4.7)
        m_moduleScope = saveScope();
    }
    completeDeferredConsts();

    PackageInfo info = publishedInfo(pkg);

    const auto existing = table.packages.find(pubName);
    if (existing == table.packages.end()) {
        table.packages.emplace(pubName, std::move(info));
        return;
    }
    // A package body (or a repeated declaration) adds to what is already published
    mergePackageInfo(existing->second, info);
}

bool Lowering::emitPrelude(const EntityUnit& entity, const Architecture& arch,
                           const GenericBindings* bindingsp) {
    m_subprograms.clear();  // Packages published these only through importPackage
    m_components.clear();
    for (const auto& use : entity.uses) importPackage(use.pkg, use.decl);
    for (const auto& use : arch.uses) importPackage(use.pkg, use.decl);

    // A type's range may call a pure function, so make the subprograms visible
    // before registering any type
    registerSubprograms(entity.subprograms, -1);
    registerSubprograms(arch.subprograms, -1);
    // The scope an inlined body evaluates against, taken before the first item that may
    // call a subprogram -- a type's range or a generic default -- and taken again below
    // once the generics and the architecture's types are registered as well
    m_moduleScope = saveScope();

    // Types are visible to the whole declarative part. A type's range may depend on
    // an integer constant, which may in turn depend on a generic, so register the
    // pieces in dependency order.
    preScanIntConstants(entity.decls);
    registerTypes(entity.types, entity.decls);
    registerComponents(entity.components);

    // Generics are constants visible to the port clause and the architecture
    if (registerGenerics(entity.generics, bindingsp, false)) return true;
    m_modp = nullptr;
    m_scope.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_ownedConsts.clear();
    m_types.clear();
    m_deferredTypes.clear();
    m_preConsts.clear();
    return false;
}

void Lowering::emitEntityDecls(const EntityUnit& entity, const Architecture& arch) {
    // With generics bound, integer constants can size the architecture's types
    preScanIntConstants(entity.decls);
    preScanIntConstants(arch.decls);

    // Entity declarative items are visible to every architecture of the entity
    int entityVarSeq = 0;
    for (const Decl& decl : entity.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            // A subprogram of this declarative part evaluates against what the unit has
            // declared before it (IEEE 1076-2008 4.7)
            m_moduleScope = saveScope();
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectUnmodelledObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            const std::string mangled
                = "__Vext_" + std::to_string(++entityVarSeq) + "__" + name.first;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
            traceDeclared(varp);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.kind == Decl::VARIABLE) m_variables.insert(name.first);
            if (decl.kind == Decl::SIGNAL && !isResolvedType(type)) {
                m_unresolvedSignals.insert(varp);
            }
            if (!decl.init) continue;
            AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
            AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
            if (!rhsp) {
                lhsp->deleteTree();
                continue;
            }
            if (!valueConforms(type, *decl.init, "initial value")) {
                rhsp->deleteTree();
                lhsp->deleteTree();
                continue;
            }
            rhsp = resizeExpr(rhsp, type, decl.flp);
            m_modp->addStmtsp(new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
        }
    }
    finishDeferredTypes();

    // The architecture's own types come after the entity's items, because a constraint may
    // name one of the entity's constants and the loop above emits them
    registerTypes(arch.types, arch.decls);
    registerComponents(arch.components);
    // A subprogram alias has to exist before the declarations, whose initializers may call it
    for (const AliasDecl& alias : arch.aliases) {
        if (!alias.hasSignature) continue;
        registerAlias(alias);
    }
    // Preliminary snapshot: declarative items may already call subprograms
    m_moduleScope = saveScope();
}

void Lowering::emitPorts(const EntityUnit& entity, const PortBoundsMap* portBoundsp) {
    int pinNum = 0;
    for (const PortDecl& port : entity.ports) {
        VType type;
        const PortBounds* boundsp = nullptr;
        if (portBoundsp) {
            const auto it = portBoundsp->find(port.names.empty() ? "" : port.names.front().first);
            if (it != portBoundsp->end()) boundsp = &it->second;
        }
        if (boundsp && boundsp->isRecord) {
            // An unconstrained record port: each element's range comes from the actual
            if (!resolveType(port.type, type) || type.kind != VType::RECORD) continue;
            for (size_t i = 0; i < boundsp->memberNames.size(); ++i) {
                for (VField& field : type.fields) {
                    if (field.name != boundsp->memberNames[i]) continue;
                    setRecordField(entity.flp, field, boundsp->memberLeft[i],
                                   boundsp->memberRight[i], boundsp->memberAscending[i]);
                }
            }
            finalizeRecordFields(entity.flp, type);
        } else if (boundsp) {
            // An unconstrained port specialized from the actual at this instance
            VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
            VSigning signing = VSigning::NOSIGN;
            if (port.type.mark == "bit_vector") kwd = VBasicDTypeKwd::BIT;
            signing = vectorSigning(port.type.mark);
            type = vectorType(entity.flp, kwd, signing,
                              std::abs(boundsp->left - boundsp->right) + 1);
            type.left = boundsp->left;
            type.right = boundsp->right;
            type.ascending = boundsp->ascending;
            type.dtp = findVector(entity.flp, kwd, signing, type.left, type.right, type.ascending);
        } else if (!resolveType(port.type, type)) {
            continue;
        }
        if (rejectUnmodelledObject(type, port.flp, /*allowUnconstrained:*/ true)) continue;
        if (port.dir == PortMode::INOUT) {
            // V3Tristate needs a four-state net; a VHDL `bit` (or bit_vector) inout
            // cannot be expressed and must be refused here, not in the net-type check
            const auto hasTwoState = [](const VType& type, const auto& self) -> bool {
                if (type.kind == VType::BIT || type.kwd == VBasicDTypeKwd::BIT) return true;
                if (type.isVector() && type.elemp) return self(*type.elemp, self);
                if (type.kind == VType::RECORD) {
                    for (const VField& field : type.fields) {
                        if (self(*field.type, self)) return true;
                    }
                }
                return false;
            };
            if (hasTwoState(type, hasTwoState)) {
                unsupportedError(port.flp, "inout port of a two-state type");
                continue;
            }
        }
        if (!type.dtp) {
            unsupportedError(port.flp, "port of type '" + port.type.mark
                                           + "' has no resolvable index constraint");
            continue;
        }
        for (const auto& name : port.names) {
            // A port is a declaration, so it hides a name a used package publishes
            if (!reserveName(name.first, name.second, port.flp)) continue;
            // An inout port is a net: V3Tristate connects its drivers and converts it to
            // the input plus output-enable/output pins of the two-state model.
            const VVarType varType = port.dir == PortMode::INOUT ? VVarType::WIRE : VVarType::PORT;
            AstVar* const varp = new AstVar{port.flp, varType, name.first, type.dtp};
            traceDeclared(varp);
            const VDirection direction = port.dir == PortMode::IN    ? VDirection::INPUT
                                         : port.dir == PortMode::OUT ? VDirection::OUTPUT
                                                                     : VDirection::INOUT;
            varp->direction(direction);
            // V3Tristate distinguishes a converted tristate input from an input that was
            // declared as one by comparing against the declared direction.
            varp->declDirection(direction);
            // The port list node the SV parser emits, so V3LinkDot marks the port and
            // records its pin number exactly as it does for a parsed ANSI port
            m_modp->addStmtsp(new AstPort{port.flp, ++pinNum, name.first});
            varp->pinNum(pinNum);
            // VHDL ports are signals, so procedural assignment is legal
            varp->declTyped(true);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (!isResolvedType(type)) m_unresolvedSignals.insert(varp);
        }
    }
}

void Lowering::emitArchitectureDecls(const Architecture& arch) {
    for (const Decl& decl : arch.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            // A subprogram of the architecture sees the constants declared before it
            m_moduleScope = saveScope();
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectUnmodelledObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, name.first, type.dtp};
            traceDeclared(varp);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.kind == Decl::VARIABLE) m_variables.insert(name.first);
            if (decl.kind == Decl::SIGNAL && !isResolvedType(type)) {
                m_unresolvedSignals.insert(varp);
            }
            if (decl.init) {
                AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
                AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
                if (!rhsp) {
                    lhsp->deleteTree();
                    continue;
                }
                if (!valueConforms(type, *decl.init, "initial value")) {
                    rhsp->deleteTree();
                    lhsp->deleteTree();
                    continue;
                }
                rhsp = resizeExpr(rhsp, type, decl.flp);
                m_modp->addStmtsp(
                    new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
            }
        }
    }
    finishDeferredTypes();
    // An object alias comes last: its mark may read a signal the loop above declares
    for (const AliasDecl& alias : arch.aliases) {
        if (alias.hasSignature) continue;
        registerAlias(alias);
    }
}

bool Lowering::emit(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
                    const GenericBindings* bindingsp, const PortBoundsMap* portBoundsp) {
    m_modp = modp;
    if (!emitPrelude(entity, arch, bindingsp)) return false;
    emitEntityDecls(entity, arch);
    emitPorts(entity, portBoundsp);
    emitArchitectureDecls(arch);

    // Subprograms of the declarative part capture the module scope as it stands here
    m_moduleScope = saveScope();
    // The entity's own statement part drives the module as well
    const bool savedPassive = m_passive;
    m_passive = true;
    for (const auto& stmtp : entity.stmts) {
        // Only passive statements belong here (IEEE 1076-2008 3.2.4); an assignment is
        // reported where it lowers, which also covers one inside a process
        if (stmtp->kind == Concurrent::INSTANCE || stmtp->kind == Concurrent::BLOCK
            || stmtp->kind == Concurrent::GEN_FOR || stmtp->kind == Concurrent::GEN_IF) {
            stmtp->flp->v3error("VHDL: instance, block or generate in the entity statement "
                                "part (IEEE 1076-2008 3.2.4).");
            continue;
        }
        emitConcurrent(*stmtp);
    }
    m_passive = savedPassive;
    for (const auto& stmtp : arch.stmts) emitConcurrent(*stmtp);
    checkDrivers();

    m_modp = nullptr;
    m_scope.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_ownedConsts.clear();
    m_types.clear();
    m_preConsts.clear();
    return true;
}

//! Type marks whose subtype a generic's actual can be checked against directly
bool isBuiltinGenericMark(const std::string& mark) {
    static const char* const marks[]
        = {"integer",   "natural",    "positive",         "boolean",           "bit",
           "std_logic", "std_ulogic", "std_logic_vector", "std_ulogic_vector", "bit_vector",
           "unsigned",  "signed",     "u_unsigned",       "u_signed"};
    for (const char* const candidate : marks) {
        if (mark == candidate) return true;
    }
    return false;
}

//! Select the architecture whose generics and declarations an instance specializes
const Architecture* Lowering::selectInstanceArchitecture(const Concurrent& conc,
                                                         const EntityUnit& entity) {
    FileLine* const flp = conc.flp;
    if (!conc.architectureName.empty()) {
        const auto byName = m_designp->archsByName.find(entity.name);
        if (byName == m_designp->archsByName.end()
            || !byName->second.count(conc.architectureName)) {
            flp->v3error("VHDL: entity '" + entity.spelling + "' has no architecture '"
                         + conc.architectureName + "'.");
            return nullptr;
        }
        return byName->second.at(conc.architectureName);
    }
    const auto archIt = m_designp->archs.find(entity.name);
    if (archIt == m_designp->archs.end()) {
        flp->v3error("VHDL: entity '" + entity.spelling
                     + "' has no architecture to specialize for this instance.");
        return nullptr;
    }
    return archIt->second;
}

void Lowering::freeGenericBindings(GenericBindings& bindings) {
    for (auto& entry : bindings) {
        if (entry.second.valuep) entry.second.valuep->deleteTree();
    }
}

bool Lowering::bindTypeGeneric(const PortAssoc& assoc, const std::string& folded,
                               GenericBindings& bindings) {
    GenericBinding binding;
    binding.isType = true;
    if (!resolveTypeActual(*assoc.actual, binding.typeValue)) {
        unsupportedError(assoc.flp, "generic type actual");
        return false;
    }
    bindings[folded] = binding;
    return true;
}

bool Lowering::bindSubprogramGeneric(const PortAssoc& assoc, const std::string& folded,
                                     GenericBindings& bindings) {
    // The actual names a subprogram visible where the instance is declared
    GenericBinding binding;
    const std::vector<const SubprogramDecl*> visible = visibleSubprograms(assoc.actual->text);
    if (visible.empty()) {
        unsupportedError(assoc.flp, "generic subprogram parameter actual");
        return false;
    }
    binding.subp = visible.back();
    bindings[folded] = binding;
    return true;
}

bool Lowering::bindRealGeneric(const PortAssoc& assoc, const std::string& folded,
                               GenericBindings& bindings) {
    // A real generic is an elaboration-time value, like a real constant
    GenericBinding binding;
    if (!evalReal(*assoc.actual, binding.realValue)) {
        unsupportedError(assoc.flp, "real generic actual");
        return false;
    }
    binding.isReal = true;
    bindings[folded] = binding;
    return true;
}

bool Lowering::bindStringGeneric(const InstanceGeneric& item, const PortAssoc& assoc,
                                 GenericBindings& bindings) {
    const Expr* const actualp = assoc.actual.get();
    std::string stringValue;
    bool found = false;
    if (actualp->kind == Expr::STRING_LIT) {
        stringValue = actualp->spelling;
        found = true;
    } else if (actualp->kind == Expr::BITS_LIT) {
        // A string of logic characters parses as a bit-string literal, e.g.
        // `InitString_g => ""` or `Mode_g => "1100"`
        stringValue = actualp->bits;
        found = true;
    } else if (actualp->kind == Expr::NAME) {
        // A string generic passed on from the enclosing unit, so its text
        // comes from that unit's generic
        const auto outer = m_stringConsts.find(actualp->text);
        if (outer != m_stringConsts.end()) {
            stringValue = outer->second;
            found = true;
        }
    }
    if (!found) {
        unsupportedError(actualp->flp, "string generic actual");
        return false;
    }
    GenericBinding binding;
    binding.isString = true;
    binding.stringValue = stringValue;
    bindings[item.folded] = binding;
    return true;
}

//! Bind a generic whose value is an integer or a typed static expression
bool Lowering::bindValueGeneric(const Concurrent& conc, const EntityUnit& entity,
                                const InstanceGeneric& item, const PortAssoc& assoc,
                                GenericBindings& bindings) {
    FileLine* const flp = conc.flp;
    const GenericDecl& gen = *item.declp;
    // The declared type resolves in the entity's scope, where the generics that
    // precede this one already carry the value bound at this instance
    ScopeSave saved = saveScope();
    const std::set<std::string> savedImported = m_imported;
    for (const auto& use : entity.uses) importPackage(use.pkg, use.decl);
    for (const TypeDecl& typeDecl : entity.types) registerType(typeDecl);
    registerGenerics(entity.generics, &bindings, true);
    // The declared type may itself call a function, whose body evaluates against the
    // module scope, so take it with this instance's bindings and the entity's packages
    m_moduleScope = saveScope();
    VType declared;
    bool haveType = false;
    if (unconstrainedVectorSpec(gen.type)) {
        // An unconstrained vector generic takes its bounds from the actual, as a
        // port of that type does
        AstNodeExpr* const valuep = lowerExpr(*assoc.actual, nullptr);
        if (valuep) {
            declared = typeOfDType(valuep->dtypep());
            valuep->deleteTree();
            haveType = declared.isVector() && declared.width > 0;
        }
    } else {
        haveType = (isBuiltinGenericMark(gen.type.mark) || m_types.count(gen.type.mark))
                   && resolveType(gen.type, declared);
        if (haveType && declared.isVector() && declared.width <= 0) {
            // A declared unconstrained array generic takes its bounds from the actual,
            // as a predefined vector one does: poc's `TAPS : natural_vector` is an
            // aggregate like `(1, 3, 5)` that names its own index range. Only the
            // bounds come from the actual; the element type stays the declared one.
            if (assoc.actual->kind == Expr::AGG_POSITIONAL
                || assoc.actual->kind == Expr::AGG_NAMED) {
                constrainAggregateType(*assoc.actual, declared);
            } else {
                AstNodeExpr* const valuep = lowerExpr(*assoc.actual, nullptr);
                if (valuep) {
                    const VType actualType = typeOfDType(valuep->dtypep());
                    valuep->deleteTree();
                    if (actualType.isVector() && actualType.width > 0) {
                        declared.left = actualType.left;
                        declared.right = actualType.right;
                        declared.ascending = actualType.ascending;
                        declared.width = actualType.width;
                        declared.dtp
                            = findVectorWidth(flp, declared.kwd, declared.signing, declared.width);
                    }
                }
            }
            haveType = declared.width > 0;
        }
    }
    restoreScope(saved);
    m_imported = savedImported;
    if (!haveType && isBuiltinGenericMark(gen.type.mark)) {
        unsupportedError(gen.flp, "generic of type '" + gen.type.mark + "'");
        return false;
    }
    if (!haveType) {
        unsupportedError(assoc.flp, "generic of type '" + gen.type.mark + "'");
        return false;
    }
    if (!valueConforms(declared, *assoc.actual, "generic actual")) return false;
    GenericBinding binding;
    if (declared.isVector()) {
        binding.valueType = declared;
        binding.hasValueType = true;
    }
    bool bound = false;
    if (declared.isInt()) {
        bound = evalInt(*assoc.actual, binding.intValue);
        binding.isInt = bound;
    }
    if (!bound) {
        AstNodeExpr* const valuep = lowerExpr(*assoc.actual, &declared);
        AstConst* const foldedp = valuep ? foldStatic(valuep) : nullptr;
        if (foldedp) {
            if (declared.width > 0 && foldedp->width() != declared.width) {
                assoc.flp->v3error("VHDL: generic actual does not have the generic's length.");
                foldedp->deleteTree();
                valuep->deleteTree();
                return false;
            }
            valuep->deleteTree();
            binding.valuep = foldedp;
            bound = true;
        } else {
            if (valuep) valuep->deleteTree();
            if (!V3Error::errorCount()) {
                unsupportedError(assoc.flp, "generic actual that is not static");
            }
            return false;
        }
    }
    bindings[item.folded] = binding;
    return true;
}

bool Lowering::bindOneGeneric(const Concurrent& conc, const EntityUnit& entity,
                              const InstanceGeneric& item, const PortAssoc* assocp,
                              GenericBindings& bindings) {
    const GenericDecl& gen = *item.declp;
    if (!assocp) {
        if (!gen.init) {
            conc.flp->v3error("VHDL: generic '" + item.spelling + "' of entity '" + entity.spelling
                              + "' has no default value and no association.");
            return false;
        }
        return true;
    }
    if (!assocp->actual) {
        assocp->flp->v3error("VHDL: 'open' is not allowed on a generic association.");
        return false;
    }
    if (gen.isType) return bindTypeGeneric(*assocp, item.folded, bindings);
    if (gen.isSubprogram) return bindSubprogramGeneric(*assocp, item.folded, bindings);
    if (gen.type.mark == "real") return bindRealGeneric(*assocp, item.folded, bindings);
    if (gen.type.mark == "string") return bindStringGeneric(item, *assocp, bindings);
    return bindValueGeneric(conc, entity, item, *assocp, bindings);
}

//! Bind every generic of the entity against the instance's generic associations
bool Lowering::bindInstanceGenerics(const Concurrent& conc, const EntityUnit& entity,
                                    GenericBindings& bindings) {
    FileLine* const flp = conc.flp;
    std::vector<InstanceGeneric> items;
    for (const GenericDecl& gen : entity.generics) {
        for (const auto& name : gen.names) items.push_back({&gen, name.first, name.second});
    }
    const std::vector<PortAssoc>& assocs = conc.genericAssociations;
    const bool positional = std::any_of(
        assocs.begin(), assocs.end(), [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    if (positional && assocs.size() > items.size()) {
        flp->v3error("VHDL: instance of '" + entity.spelling
                     + "' binds more generics than the entity has.");
        return false;
    }
    if (!positional) {
        for (const PortAssoc& assoc : assocs) {
            const bool known
                = std::any_of(items.begin(), items.end(), [&](const InstanceGeneric& item) {
                      return item.folded == assoc.formal;
                  });
            if (!known) {
                assoc.flp->v3error("VHDL: entity '" + entity.spelling + "' has no generic '"
                                   + assoc.formalSpelling + "'.");
                return false;
            }
        }
    }
    for (size_t k = 0; k < items.size(); ++k) {
        const PortAssoc* assocp = nullptr;
        if (positional) {
            if (k < assocs.size()) assocp = &assocs[k];
        } else {
            for (const PortAssoc& assoc : assocs) {
                if (assoc.formal == items[k].folded) assocp = &assoc;
            }
        }
        if (!bindOneGeneric(conc, entity, items[k], assocp, bindings)) {
            freeGenericBindings(bindings);
            return false;
        }
    }
    return true;
}

//! Bind an instance's generics and its unconstrained port ranges, and return the module
//! specialized for that binding; the base module when there is nothing to bind
AstModule* Lowering::specializeInstance(const Concurrent& conc, const EntityUnit& entity,
                                        AstModule* basep, const PortBoundsMap& portBounds) {
    FileLine* const flp = conc.flp;
    const Architecture* const archp = selectInstanceArchitecture(conc, entity);
    if (!archp) return nullptr;
    GenericBindings bindings;
    if (!bindInstanceGenerics(conc, entity, bindings)) return nullptr;
    if (bindings.empty() && portBounds.empty()) {
        // Nothing to bind: an instance uses the entity's own module
        m_designp->baseUsed.insert(entity.name);
        return basep;  // m_instanceBindings is already empty
    }
    SpecKey key;
    key.entity = entity.name;
    // portPart is kept separate so a coverage test can distinguish port specializations
    if (!portBounds.empty()) {
        key.portPart += "|ports";
        for (const auto& entry : portBounds) {
            const PortBounds& bounds = entry.second;
            key.portPart += "|" + entry.first + "=";
            if (bounds.isRecord) {
                for (size_t i = 0; i < bounds.memberNames.size(); ++i) {
                    key.portPart += bounds.memberNames[i] + "("
                                    + std::to_string(bounds.memberLeft[i]) + ":"
                                    + std::to_string(bounds.memberRight[i])
                                    + (bounds.memberAscending[i] ? "to" : "downto") + ")";
                }
            } else {
                key.portPart += std::to_string(bounds.left) + ":" + std::to_string(bounds.right)
                                + ":" + (bounds.ascending ? "to" : "downto");
            }
        }
    }
    key.arch = archp->name;
    for (const auto& entry : bindings) {
        key.genericPart += "|" + entry.first + "=";
        if (entry.second.isType) {
            const VType& type = entry.second.typeValue;
            key.genericPart += std::to_string(static_cast<int>(type.kind)) + ":"
                               + std::to_string(type.width) + ":" + std::to_string(type.left) + ":"
                               + std::to_string(type.right) + ":"
                               + (type.ascending ? "to" : "downto") + ":"
                               + std::to_string(static_cast<int>(type.signing));
        } else if (entry.second.isInt) {
            key.genericPart += std::to_string(entry.second.intValue);
        } else if (entry.second.isString) {
            key.genericPart += entry.second.stringValue;
        } else if (entry.second.isReal) {
            // A real is an elaboration-time value with no constant node; the shortest
            // representation that round-trips keeps distinct values apart
            char text[40];
            VL_SNPRINTF(text, sizeof(text), "%.17g", entry.second.realValue);
            key.genericPart += text;
        } else if (entry.second.subp) {
            // Unique per actual subprogram declaration within this run
            key.genericPart
                += "subp:" + std::to_string(reinterpret_cast<uintptr_t>(entry.second.subp));
        } else {
            key.genericPart += entry.second.valuep->num().ascii(false);
        }
    }
    auto& cache = m_designp->specializations;
    const auto found = cache.find(key);
    if (found != cache.end()) {
        AstModule* const cachedp = found->second;
        // The port map of this instance still lowers its defaults: keep the bindings
        m_instanceBindings = std::move(bindings);
        return cachedp;  // Null when an earlier specialization of this key failed
    }
    if (++m_designp->specDepth > 32) {
        unsupportedError(flp, "generic binding recursion that does not terminate");
        freeGenericBindings(bindings);
        return nullptr;
    }
    AstModule* const specp
        = new AstModule{entity.flp, entity.name + "__Vspec" + std::to_string(++m_designp->specSeq),
                        entity.libname};
    specp->inLibrary(entity.inLibrary);
    m_rootp->addModulesp(specp);
    Lowering child{m_rootp, m_packagesp, m_modulesp, m_designp};
    const bool ok = child.emit(entity, *archp, specp, bindings.empty() ? nullptr : &bindings,
                               portBounds.empty() ? nullptr : &portBounds);
    --m_designp->specDepth;
    // The port map of this instance still lowers its defaults: keep the bindings
    m_instanceBindings = std::move(bindings);
    cache[key] = ok ? specp : nullptr;
    return ok ? specp : nullptr;
}

bool Lowering::resolveTypeActual(const Expr& actual, VType& out) {
    TypeSpec spec;
    spec.flp = actual.flp;
    if (actual.kind == Expr::NAME) {
        spec.mark = actual.text;
        return resolveType(spec, out);
    }
    if ((actual.kind == Expr::SLICE || actual.kind == Expr::INDEX) && actual.a
        && actual.a->kind == Expr::NAME) {
        spec.mark = actual.a->text;
        if (actual.kind != Expr::SLICE || !actual.b || !actual.c) return false;
        spec.hasRange = true;
        spec.ascending = actual.text == "to";
        spec.left = cloneExpr(*actual.b);
        spec.right = cloneExpr(*actual.c);
        return resolveType(spec, out);
    }
    return false;
}

bool Lowering::derivePortBounds(const EntityUnit& entity, const Concurrent& conc,
                                const PortDecl& port, PortBounds& out) {
    const std::string& portName = port.names.front().first;
    int targetIndex = -1;
    int index = 0;
    for (const PortDecl& candidate : entity.ports) {
        for (const auto& name : candidate.names) {
            if (name.first == portName) targetIndex = index;
            ++index;
        }
    }
    const bool positional
        = std::any_of(conc.associations.begin(), conc.associations.end(),
                      [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    std::vector<const PortAssoc*> assocs;
    for (size_t i = 0; i < conc.associations.size(); ++i) {
        const PortAssoc& assoc = conc.associations[i];
        if (positional ? static_cast<int>(i) == targetIndex
                       : assoc.formal == port.names.front().first) {
            assocs.push_back(&assoc);
        }
    }
    bool have = false;
    bool ascending = false;
    int left = 0;
    int right = 0;
    bool ranges = false;
    bool wholes = false;
    const auto addBounds = [&](int64_t l, int64_t r, bool asc) -> bool {
        if (asc != ascending && have) return false;
        ascending = asc;
        const int lo = static_cast<int>(std::min(l, r));
        const int hi = static_cast<int>(std::max(l, r));
        if (!have) {
            left = asc ? lo : hi;
            right = asc ? hi : lo;
            have = true;
            return true;
        }
        left = asc ? std::min(left, lo) : std::max(left, hi);
        right = asc ? std::max(right, hi) : std::min(right, lo);
        return true;
    };
    for (const PortAssoc* const assocp : assocs) {
        if (assocp->hasFormalRange) {
            int64_t l = 0;
            int64_t r = 0;
            if (!staticInt(assocp->formalLeft, l)
                || (assocp->formalRight && !staticInt(assocp->formalRight, r))) {
                unsupportedError(assocp->flp, "non-static port association range");
                return false;
            }
            if (!assocp->formalRight) r = l;
            if (!addBounds(l, r, assocp->formalAscending)) {
                unsupportedError(assocp->flp, "mixed directions on an unconstrained port");
                return false;
            }
            ranges = true;
            continue;
        }
        if (!assocp->actual) {
            if (!port.init) {
                unsupportedError(assocp->flp, "open unconstrained port with no default value");
                return false;
            }
            AstNodeExpr* const valuep = lowerExpr(*port.init, nullptr);
            if (!valuep) return false;
            const VType type = typeOfDType(valuep->dtypep());
            valuep->deleteTree();
            if (!type.isVector() || !addBounds(type.left, type.right, type.ascending)) {
                unsupportedError(assocp->flp, "default value of an unconstrained port");
                return false;
            }
            wholes = true;
            continue;
        }
        VType type;
        if (!typeOfDesignator(*assocp->actual, type)) {
            AstNodeExpr* const valuep = lowerExpr(*assocp->actual, nullptr);
            if (!valuep) return false;
            type = typeOfDType(valuep->dtypep());
            valuep->deleteTree();
        }
        if (!type.isVector() || !addBounds(type.left, type.right, type.ascending)) {
            unsupportedError(assocp->flp, "actual associated with an unconstrained port");
            return false;
        }
        wholes = true;
    }
    if (!have && port.init) {
        // An omitted input port takes its declared default, whose type fixes the range
        AstNodeExpr* const valuep = lowerExpr(*port.init, nullptr);
        if (!valuep) return false;
        const VType type = typeOfDType(valuep->dtypep());
        valuep->deleteTree();
        if (!type.isVector() || !addBounds(type.left, type.right, type.ascending)) {
            unsupportedError(port.flp, "default value of an unconstrained port");
            return false;
        }
    }
    if (!have) {
        unsupportedError(port.flp, "unconstrained port '" + port.names.front().second
                                       + "' with no association");
        return false;
    }
    if (ranges && wholes) {
        unsupportedError(port.flp, "mixed whole and part associations of an unconstrained port");
        return false;
    }
    out.left = left;
    out.right = right;
    out.ascending = ascending;
    return true;
}

bool Lowering::resolvePortRecordType(const EntityUnit& entity, const PortDecl& port,
                                     VType& out) const {
    if (port.type.hasRange || !port.type.constraintMark.empty()) return false;
    const auto typeIt = m_types.find(port.type.mark);
    if (typeIt != m_types.end() && typeIt->second.kind == VType::RECORD) {
        out = typeIt->second;
        return true;
    }
    // The parent can name a record's elements without importing the package that declares
    // the record, so fall back to the child's own context.
    if (m_packagesp) {
        for (const auto& use : entity.uses) {
            const auto pkgIt = m_packagesp->packages.find(use.pkg);
            if (pkgIt == m_packagesp->packages.end()) continue;
            const auto fieldIt = pkgIt->second.types.find(port.type.mark);
            if (fieldIt != pkgIt->second.types.end() && fieldIt->second.kind == VType::RECORD) {
                out = fieldIt->second;
                return true;
            }
        }
    }
    return false;
}

bool Lowering::deriveRecordBounds(const EntityUnit& entity, const Concurrent& conc,
                                  const PortDecl& port, PortBounds& out) {
    const std::string& portName = port.names.front().first;
    int targetIndex = -1;
    int index = 0;
    for (const PortDecl& candidate : entity.ports) {
        for (const auto& name : candidate.names) {
            if (name.first == portName) targetIndex = index;
            ++index;
        }
    }
    const bool positional
        = std::any_of(conc.associations.begin(), conc.associations.end(),
                      [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    std::vector<const PortAssoc*> assocs;
    for (size_t i = 0; i < conc.associations.size(); ++i) {
        const PortAssoc& assoc = conc.associations[i];
        if (positional ? static_cast<int>(i) == targetIndex
                       : assoc.formal == port.names.front().first) {
            assocs.push_back(&assoc);
        }
    }
    const bool anyWhole = std::any_of(assocs.begin(), assocs.end(), [](const PortAssoc* assocp) {
        return assocp->formalMember.empty();
    });
    const bool anyMember = std::any_of(assocs.begin(), assocs.end(), [](const PortAssoc* assocp) {
        return !assocp->formalMember.empty();
    });
    if (anyWhole && anyMember) {
        unsupportedError(conc.flp, "mixing whole-port and record-member associations");
        return false;
    }
    if (anyWhole && assocs.size() > 1) {
        conc.flp->v3error("VHDL: port '" + port.names.front().second
                          + "' is associated more than once.");
        return false;
    }
    // The port's declared record fixes the element order and its constrained elements.
    // Each unconstrained element takes its range from the association that names it.
    VType baseType;
    if (!resolvePortRecordType(entity, port, baseType)) {
        unsupportedError(port.flp, "unconstrained record port");
        return false;
    }
    const size_t fieldCount = baseType.fields.size();
    std::vector<int> lefts(fieldCount);
    std::vector<int> rights(fieldCount);
    std::vector<bool> ascendings(fieldCount);
    std::vector<bool> have(fieldCount, false);
    std::vector<bool> seen(fieldCount, false);
    const auto fieldIndex = [&](const std::string& name) {
        for (size_t i = 0; i < fieldCount; ++i) {
            if (baseType.fields[i].name == name) return static_cast<int>(i);
        }
        return -1;
    };
    const auto actualTypeOf = [&](const Expr& actual, VType& type) {
        if (typeOfDesignator(actual, type)) return true;
        AstNodeExpr* const valuep = lowerExpr(actual, nullptr);
        if (!valuep) return false;
        type = typeOfDType(valuep->dtypep());
        valuep->deleteTree();
        return true;
    };
    const auto setMember = [&](const int field, const VType& type) {
        lefts[field] = type.left;
        rights[field] = type.right;
        ascendings[field] = type.ascending;
        have[field] = true;
    };
    for (const PortAssoc* const assocp : assocs) {
        if (assocp->hasFormalRange) {
            unsupportedError(assocp->flp, "part of a port that is not a bit vector");
            return false;
        }
        if (!assocp->formalMember.empty()) {
            const int field = fieldIndex(assocp->formalMember);
            if (field < 0) {
                assocp->flp->v3error("VHDL: record port '" + port.names.front().second
                                     + "' has no element '" + assocp->formalMemberSpelling + "'.");
                return false;
            }
            if (seen[field]) {
                assocp->flp->v3error("VHDL: record port '" + port.names.front().second
                                     + "' element '" + assocp->formalMemberSpelling
                                     + "' is associated more than once.");
                return false;
            }
            seen[field] = true;
            if (baseType.fields[field].width != 0) continue;  // range from the declaration
            if (!assocp->actual) {
                unsupportedError(assocp->flp, "open record-member port association");
                return false;
            }
            VType actualType;
            if (!actualTypeOf(*assocp->actual, actualType)) return false;
            if (!actualType.isVector()) {
                unsupportedError(assocp->flp, "record port element actual is not a vector");
                return false;
            }
            setMember(field, actualType);
            continue;
        }
        // A whole record actual fixes every unconstrained element's range
        if (!assocp->actual) {
            unsupportedError(assocp->flp, "open unconstrained record port with no default value");
            return false;
        }
        VType actualType;
        if (!actualTypeOf(*assocp->actual, actualType)) return false;
        if (actualType.kind != VType::RECORD) {
            unsupportedError(assocp->flp,
                             "actual of an unconstrained record port is not a record");
            return false;
        }
        for (size_t field = 0; field < fieldCount; ++field) {
            seen[field] = true;
            if (baseType.fields[field].width != 0) continue;
            const VField* actualp = nullptr;
            for (const VField& f : actualType.fields) {
                if (f.name == baseType.fields[field].name) actualp = &f;
            }
            if (!actualp || !actualp->type || !actualp->type->isVector()) {
                unsupportedError(assocp->flp,
                                 "unconstrained record port element has no vector actual");
                return false;
            }
            setMember(static_cast<int>(field), *actualp->type);
        }
    }
    out.isRecord = true;
    for (size_t field = 0; field < fieldCount; ++field) {
        if (baseType.fields[field].width != 0) continue;
        if (!have[field]) {
            unsupportedError(port.flp, "unconstrained record port element '"
                                           + baseType.fields[field].name + "' has no association");
            return false;
        }
        out.memberNames.push_back(baseType.fields[field].name);
        out.memberLeft.push_back(lefts[field]);
        out.memberRight.push_back(rights[field]);
        out.memberAscending.push_back(ascendings[field]);
    }
    return true;
}

//! Find the instantiated entity and check the instance's generic map against its interface
bool Lowering::lookupInstanceModule(const Concurrent& conc, AstModule*& modp,
                                    const EntityUnit*& entityp) {
    FileLine* const flp = conc.flp;
    freeInstanceBindings();
    if (!m_modulesp) {
        unsupportedError(flp, "instantiation without an elaborated design");
        return false;
    }
    const auto modIt = m_modulesp->find(conc.instanceName);
    if (modIt == m_modulesp->end()) {
        flp->v3error("Cannot find VHDL entity '" + conc.instanceName + "' for instance '"
                         + conc.instanceLabel + "'\n"
                     << flp->warnMore()
                     << "... VHDL instantiates only entities analyzed with it; a SystemVerilog"
                        " module must be instantiated from an SV top, with this hierarchy built"
                        " by --lib-create.");
        return false;
    }
    modp = modIt->second;
    if (m_designp) {
        const auto entityIt = m_designp->entities.find(conc.instanceName);
        if (entityIt != m_designp->entities.end()) entityp = entityIt->second;
    }
    if (!conc.genericAssociations.empty() && (!entityp || entityp->generics.empty())) {
        flp->v3error("VHDL: generic map on instance '" + conc.instanceLabel + "' of '"
                     + conc.instanceName + "', which has no generics.");
        return false;
    }
    return true;
}

//! A component declaration's interface must conform to the entity the instance binds,
//! and the association formals are the component's (IEEE 1076-2008 6.5.2.2)
bool Lowering::checkComponentConformance(const Concurrent& conc, const EntityUnit& entity) {
    const auto componentIt = m_components.find(conc.instanceName);
    if (componentIt == m_components.end()) return true;
    const ComponentDecl& component = *componentIt->second;
    const auto sameBase = [](const TypeSpec& lhs, const TypeSpec& rhs) {
        return lhs.pkg == rhs.pkg && lhs.mark == rhs.mark && lhs.objectName == rhs.objectName
               && lhs.elementMark == rhs.elementMark;
    };
    bool ok = true;
    std::set<std::string> componentPorts;
    for (const PortDecl& port : component.ports) {
        for (const auto& name : port.names) {
            componentPorts.insert(name.first);
            const PortDecl* entityPortp = nullptr;
            for (const PortDecl& entityPort : entity.ports) {
                for (const auto& entityName : entityPort.names) {
                    if (entityName.first == name.first) entityPortp = &entityPort;
                }
            }
            if (!entityPortp) {
                port.flp->v3error("VHDL: component '" + component.spelling + "' declares port '"
                                  + name.second + "', which entity '" + entity.spelling
                                  + "' does not have.");
                ok = false;
                continue;
            }
            if (entityPortp->dir != port.dir) {
                port.flp->v3error("VHDL: port '" + name.second + "' of component '"
                                  + component.spelling + "' has a different mode from entity '"
                                  + entity.spelling + "'.");
                ok = false;
            }
            if (!sameBase(entityPortp->type, port.type)) {
                port.flp->v3error("VHDL: port '" + name.second + "' of component '"
                                  + component.spelling + "' has a different type from entity '"
                                  + entity.spelling + "'.");
                ok = false;
            }
        }
    }
    for (const PortDecl& port : entity.ports) {
        for (const auto& name : port.names) {
            if (componentPorts.count(name.first)) continue;
            port.flp->v3error("VHDL: entity '" + entity.spelling + "' has port '" + name.second
                              + "', which component '" + component.spelling
                              + "' does not declare.");
            ok = false;
        }
    }
    std::map<std::string, const GenericDecl*> componentGenerics;
    for (const GenericDecl& generic : component.generics) {
        for (const auto& name : generic.names) componentGenerics[name.first] = &generic;
    }
    std::set<std::string> entityGenerics;
    for (const GenericDecl& generic : entity.generics) {
        for (const auto& name : generic.names) entityGenerics.insert(name.first);
    }
    for (const auto& entry : componentGenerics) {
        const GenericDecl* entityGenericp = nullptr;
        for (const GenericDecl& generic : entity.generics) {
            for (const auto& name : generic.names) {
                if (name.first == entry.first) entityGenericp = &generic;
            }
        }
        if (!entityGenericp) {
            entry.second->flp->v3error("VHDL: component '" + component.spelling
                                       + "' declares generic '" + entry.first + "', which entity '"
                                       + entity.spelling + "' does not have.");
            ok = false;
            continue;
        }
        const bool sameKind = entry.second->isType == entityGenericp->isType
                              && entry.second->isSubprogram == entityGenericp->isSubprogram;
        if (!sameKind
            || (!entry.second->isType && !entry.second->isSubprogram
                && !sameBase(entry.second->type, entityGenericp->type))) {
            entry.second->flp->v3error("VHDL: generic '" + entry.first + "' of component '"
                                       + component.spelling + "' does not conform to entity '"
                                       + entity.spelling + "'.");
            ok = false;
        }
    }
    for (const std::string& name : entityGenerics) {
        if (componentGenerics.count(name)) continue;
        entity.flp->v3error("VHDL: entity '" + entity.spelling + "' has generic '" + name
                            + "', which component '" + component.spelling + "' does not declare.");
        ok = false;
    }
    for (const PortAssoc& assoc : conc.associations) {
        if (assoc.formal.empty() || componentPorts.count(assoc.formal)) continue;
        assoc.flp->v3error("VHDL: component '" + component.spelling + "' has no port '"
                           + assoc.formalSpelling + "'.");
        ok = false;
    }
    return ok;
}

//! Select the pre-lowered module when an instance names a non-default architecture
bool Lowering::selectInstanceArchitecture(const Concurrent& conc, const EntityUnit* entityp,
                                          AstModule*& modp) {
    FileLine* const flp = conc.flp;
    if (!conc.architectureName.empty()) {
        const bool known
            = entityp && m_designp && m_designp->archsByName.count(conc.instanceName)
              && m_designp->archsByName.at(conc.instanceName).count(conc.architectureName);
        if (!known) {
            flp->v3error("VHDL: entity '" + conc.instanceName + "' has no architecture '"
                         + conc.architectureName + "'.");
            return false;
        }
        // A specialized module selects its architecture itself; the base module's is the
        // default, so only a different one needs the pre-lowered module
        if (!entityNeedsSpecialization(*entityp, *m_designp)) {
            const auto defaultIt = m_designp->archs.find(conc.instanceName);
            const bool isDefault = defaultIt != m_designp->archs.end()
                                   && defaultIt->second->name == conc.architectureName;
            if (!isDefault) {
                const auto moduleIt
                    = m_designp->archModules.find({conc.instanceName, conc.architectureName});
                // createSelectedArchModules lowered one for every such selection
                UASSERT(moduleIt != m_designp->archModules.end(),
                        "No module lowered for a selected architecture");
                modp = moduleIt->second;
            }
        }
    }
    return true;
}

//! Derive unconstrained port ranges at this instance and specialize the module
bool Lowering::specializeInstancePorts(const Concurrent& conc, const EntityUnit* entityp,
                                       AstModule*& modp, PortBoundsMap& portBounds) {
    if (entityp) m_designp->instantiated.insert(conc.instanceName);
    if (entityp) {
        for (const PortDecl& port : entityp->ports) {
            const bool unconstrainedRange
                = !port.type.hasRange && port.type.constraintMark.empty();
            const bool isRecordPort
                = unconstrainedRange && !unconstrainedPort(port)
                  && m_designp->unconstrainedRecordMembers.count(port.type.mark);
            if (!unconstrainedPort(port) && !isRecordPort) continue;
            PortBounds bounds;
            const bool ok = isRecordPort ? deriveRecordBounds(*entityp, conc, port, bounds)
                                         : derivePortBounds(*entityp, conc, port, bounds);
            if (!ok) return false;
            for (const auto& name : port.names) portBounds[name.first] = bounds;
        }
        if (!entityp->generics.empty() || !portBounds.empty()) {
            modp = specializeInstance(conc, *entityp, modp, portBounds);
            if (!modp) return false;
        } else {
            m_designp->baseUsed.insert(entityp->name);
        }
    }
    return true;
}

//! Collect the module's ports in pin order and reject excess positional associations
bool Lowering::collectInstancePorts(const Concurrent& conc, AstModule* modp,
                                    std::vector<AstVar*>& ports, bool& positional) {
    FileLine* const flp = conc.flp;
    for (AstNode* const itemp : modp->stmtspList()) {
        AstVar* const varp = VN_CAST(itemp, Var);
        if (varp && varp->isIO()) ports.push_back(varp);
    }
    std::sort(ports.begin(), ports.end(),
              [](const AstVar* lp, const AstVar* rp) { return lp->pinNum() < rp->pinNum(); });
    // Associations without a formal name are positional: they map to the next port
    positional = std::any_of(conc.associations.begin(), conc.associations.end(),
                             [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    if (positional && conc.associations.size() > ports.size()) {
        flp->v3error("VHDL: instance of '" + conc.instanceName
                     + "' associates more ports than "
                       "the entity has.");
        return false;
    }
    return true;
}

//! The record type a port was declared with, if any, for member associations
VType Lowering::instanceRecordPortType(const EntityUnit* entityp, const AstVar* portp) const {
    VType none;
    if (!entityp) return none;
    for (const PortDecl& port : entityp->ports) {
        const bool match = std::any_of(port.names.begin(), port.names.end(),
                                       [&](const std::pair<std::string, std::string>& name) {
                                           return name.first == portp->name();
                                       });
        if (!match) continue;
        VType found;
        return resolvePortRecordType(*entityp, port, found) ? found : VType{};
    }
    return none;
}

const VField* Lowering::findRecordField(const VType& rec, const std::string& member) {
    for (const VField& field : rec.fields)
        if (field.name == member) return &field;
    return nullptr;
}

//! An unassociated or `open` input takes its declared default; anything else
//! stays unconnected (IEEE 1076-2008 6.5.7.1)
AstNodeExpr* Lowering::instanceDeclaredDefault(const Concurrent& conc, const EntityUnit* entityp,
                                               const AstVar* portp, const VType& aggType,
                                               const VType& portType, bool& defaultError) {
    FileLine* const flp = conc.flp;
    if (!entityp || !portp->direction().isInput()) return nullptr;
    for (const PortDecl& port : entityp->ports) {
        if (!port.init) continue;
        const bool matches = std::any_of(port.names.begin(), port.names.end(),
                                         [&](const std::pair<std::string, std::string>& name) {
                                             return name.first == portp->name();
                                         });
        if (!matches) continue;
        // The default is written in the entity's scope, so its packages are visible
        // there even when the instance does not import them, and its generics carry
        // the values this instance binds (IEEE 1076-2008 6.5.7.1)
        bool conforms = false;
        AstNodeExpr* defaultp = nullptr;
        {
            ScopeSave saved = saveScope();
            const std::set<std::string> savedImported = m_imported;
            for (const auto& use : entityp->uses) importPackage(use.pkg, use.decl);
            for (const TypeDecl& typeDecl : entityp->types) registerType(typeDecl);
            registerGenerics(entityp->generics, &m_instanceBindings, true);
            conforms = valueConforms(aggType, *port.init, "port default");
            if (conforms) defaultp = lowerExpr(*port.init, &aggType);
            restoreScope(saved);
            m_imported = savedImported;
        }
        if (!conforms || !defaultp) {
            defaultError = true;
            return nullptr;
        }
        if (aggType.width > 0 && defaultp->width() != aggType.width) {
            port.flp->v3error("VHDL: default of port " + portp->prettyNameQ()
                              + " does not have the port's length (IEEE 1076-2008 6.5.7.1).");
            defaultp->deleteTree();
            defaultError = true;
            return nullptr;
        }
        return resizeExpr(defaultp, portType, flp);
    }
    return nullptr;
}

//! Bind a record port whose individual elements are associated
bool Lowering::bindRecordMemberPort(InstancePortBind& ctx, AstVar* portp,
                                    const std::vector<const PortAssoc*>& assocs,
                                    const VType& recType, AstNodeExpr*& actualp) {
    FileLine* const flp = ctx.conc.flp;
    if (recType.kind != VType::RECORD) {
        unsupportedError(flp, "record-member association on a non-record port");
        return false;
    }
    for (const PortAssoc* const assocp : assocs) {
        if (assocp->formalMember.empty()) {
            unsupportedError(assocp->flp, "mixing whole-port and record-member associations");
            return false;
        }
    }
    if (portp->direction().isOutput()) {
        // Each member actual is driven from its element of a packed temp the instance
        // writes, mirroring how the instance reads and writes record elements
        AstVar* const tempVarp = new AstVar{
            flp, VVarType::VAR, "__Vind_" + std::to_string(++m_loopSeq) + "__" + portp->name(),
            portp->dtypep()};
        m_modp->addStmtsp(tempVarp);
        AstNodeStmt* headp = nullptr;
        for (const PortAssoc* const assocp : assocs) {
            if (!assocp->actual) continue;
            const VField* const fieldp = findRecordField(recType, assocp->formalMember);
            if (!fieldp) {
                assocp->flp->v3error("VHDL: record port " + portp->prettyNameQ()
                                     + " has no element '" + assocp->formalMemberSpelling + "'.");
                if (headp) headp->deleteTree();
                return false;
            }
            AstNodeExpr* const lhsp = lowerDesignator(*assocp->actual, true);
            if (!lhsp) {
                if (headp) headp->deleteTree();
                return false;
            }
            AstSel* const rhsp = new AstSel{flp, new AstVarRef{flp, tempVarp, VAccess::READ},
                                            fieldp->offset, fieldp->width};
            rhsp->dtypep(fieldp->type->dtp);
            AstNodeStmt* const assp = new AstAssign{flp, lhsp, rhsp};
            headp = headp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, assp) : assp;
        }
        if (headp) {
            m_modp->addStmtsp(new AstAlways{flp, VAlwaysKwd::CONT_ASSIGN, nullptr, headp});
        }
        actualp = new AstVarRef{flp, tempVarp, VAccess::WRITE};
    } else {
        // An input record packs its member actuals exactly like a record aggregate
        Expr agg{Expr::AGG_NAMED};
        agg.flp = flp;
        for (const PortAssoc* const assocp : assocs) {
            if (!assocp->actual) {
                unsupportedError(assocp->flp, "open record-member port association");
                return false;
            }
            ExprP choicep = std::make_unique<Expr>(Expr::NAME);
            choicep->flp = flp;
            choicep->text = assocp->formalMember;
            choicep->spelling = assocp->formalMemberSpelling;
            agg.choices.emplace_back();
            agg.choices.back().push_back(std::move(choicep));
            agg.args.push_back(cloneExpr(*assocp->actual));
        }
        actualp = lowerAggregate(agg, recType);
        if (!actualp) return false;
    }
    return true;
}

//! Bind a whole port to one actual, or take its declared default for `open`
bool Lowering::bindWholePort(InstancePortBind& ctx, AstVar* portp, const PortAssoc& assoc,
                             const VType& aggType, const VType& portType, AstNodeExpr*& actualp) {
    FileLine* const flp = ctx.conc.flp;
    // The whole port connects to one actual; an `open` association leaves
    // it unconnected, so an input takes its declared default
    if (assoc.actual) {
        // IEEE 1076-2008 6.5.2.2: the actual must conform to the formal's
        // type. A scalar of another base type is a user error, not a resize.
        if (!valueConforms(aggType, *assoc.actual, "port actual")) return false;
        const bool driven = portp->direction().isOutput() || portp->direction().isInout();
        AstNodeExpr* valuep
            = driven ? lowerDesignator(*assoc.actual, true) : lowerExpr(*assoc.actual, &aggType);
        if (!valuep) return false;
        // valueConforms cannot see the element kind of a packed vector, and
        // cannot size a literal or a context-typed call
        const VType actualType = typeOfLowered(*assoc.actual, valuep);
        bool conforms = true;
        if (actualType.isVector() && portType.isVector()) {
            conforms = actualType.width == portType.width;
            if (conforms && assoc.actual->kind == Expr::NAME) {
                conforms = actualType.kwd == portType.kwd;
            }
        }
        if (!conforms) {
            assoc.flp->v3error("VHDL: actual of port " + portp->prettyNameQ()
                               + " does not conform to the port's type "
                                 "(IEEE 1076-2008 6.5.2.2).");
            valuep->deleteTree();
            return false;
        }
        if (aggType.width > 0 && valuep->width() != aggType.width) {
            assoc.flp->v3error("VHDL: actual of port " + portp->prettyNameQ()
                               + " does not have the port's length "
                                 "(IEEE 1076-2008 6.5.2.2).");
            valuep->deleteTree();
            return false;
        }
        actualp = resizeExpr(valuep, portType, flp);
    } else {
        actualp = instanceDeclaredDefault(ctx.conc, ctx.entityp, portp, aggType, portType,
                                          ctx.defaultError);
        if (!actualp && !ctx.defaultError && portp->direction().isInput()) {
            assoc.flp->v3error("VHDL: input port " + portp->prettyNameQ()
                               + " is left open and has no default value.");
            return false;
        }
    }
    return true;
}

//! Bind the individual bit ranges of a vector port
bool Lowering::bindPartialPort(InstancePortBind& ctx, AstVar* portp,
                               const std::vector<const PortAssoc*>& assocs, const VType& portType,
                               AstNodeExpr*& actualp) {
    FileLine* const flp = ctx.conc.flp;
    //! One association of a port, resolved to the bit range it connects
    struct Connection final {
        int lsb = 0;  // Least significant bit of the connected range
        int width = 1;  // Width of the connected range
        const PortAssoc* assocp = nullptr;  // The association that connects the range
    };
    // Collect the bit range each association connects
    std::vector<Connection> connections;
    bool bad = false;
    for (const PortAssoc* const assocp : assocs) {
        int64_t left = 0;
        int64_t right = 0;
        if (!assocp->hasFormalRange) {
            unsupportedError(assocp->flp, "mixing whole-port and part-port associations");
            bad = true;
            break;
        }
        const bool haveRight = assocp->formalRight != nullptr;
        if (!staticInt(assocp->formalLeft, left)
            || (haveRight && !staticInt(assocp->formalRight, right))) {
            unsupportedError(assocp->flp, "non-static port association range");
            bad = true;
            break;
        }
        if (!haveRight) right = left;
        Connection connection;
        const int offsetLeft = portType.indexOffset(static_cast<int>(left));
        const int offsetRight = portType.indexOffset(static_cast<int>(right));
        connection.lsb = std::min(offsetLeft, offsetRight);
        connection.width = std::abs(offsetLeft - offsetRight) + 1;
        if (connection.lsb < 0 || connection.lsb + connection.width > portType.width) {
            assocp->flp->v3error("VHDL: association is out of range of port "
                                 + portp->prettyNameQ() + ".");
            bad = true;
            break;
        }
        connection.assocp = assocp;
        connections.push_back(connection);
    }
    if (bad) return false;
    if (portp->direction().isInput()) {
        // Concatenate from the most significant connected range down
        std::sort(connections.begin(), connections.end(),
                  [](const Connection& lhs, const Connection& rhs) { return lhs.lsb > rhs.lsb; });
        for (const Connection& connection : connections) {
            AstNodeExpr* valuep = nullptr;
            if (connection.assocp->actual) {
                const VType elementType
                    = vectorType(flp, portType.kwd, VSigning::NOSIGN, connection.width);
                valuep = lowerExpr(*connection.assocp->actual, &elementType);
                if (!valuep) return false;
                valuep = resizeExpr(valuep, elementType, flp);
            } else {
                // An open input element contributes unknown bits
                valuep = makeRepeatedConst(
                    flp, vectorType(flp, portType.kwd, VSigning::NOSIGN, connection.width), 'x');
            }
            actualp = actualp ? static_cast<AstNodeExpr*>(new AstConcat{flp, actualp, valuep})
                              : valuep;
        }
        actualp->dtypep(portp->dtypep());
    } else {
        // Outputs: each element actual is driven from its slice of a temp
        AstVar* const tempVarp = new AstVar{
            flp, VVarType::VAR, "__Vind_" + std::to_string(++m_loopSeq) + "__" + portp->name(),
            portp->dtypep()};
        m_modp->addStmtsp(tempVarp);
        AstNodeStmt* headp = nullptr;
        for (const Connection& connection : connections) {
            if (!connection.assocp->actual) continue;
            AstNodeExpr* const lhsp = lowerDesignator(*connection.assocp->actual, true);
            if (!lhsp) {
                if (headp) headp->deleteTree();
                return false;
            }
            const VType elementType
                = vectorType(flp, portType.kwd, VSigning::NOSIGN, connection.width);
            AstSel* const rhsp = new AstSel{flp, new AstVarRef{flp, tempVarp, VAccess::READ},
                                            connection.lsb, connection.width};
            rhsp->dtypep(elementType.dtp);
            AstNodeStmt* const assp = new AstAssign{flp, resizeExpr(lhsp, elementType, flp), rhsp};
            headp = headp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, assp) : assp;
        }
        if (headp) {
            m_modp->addStmtsp(new AstAlways{flp, VAlwaysKwd::CONT_ASSIGN, nullptr, headp});
        }
        actualp = new AstVarRef{flp, tempVarp, VAccess::WRITE};
    }
    return true;
}

//! Bind one port's associations and append the resulting pin
bool Lowering::bindInstancePort(InstancePortBind& ctx, AstVar* portp, size_t portIndex,
                                AstPin*& pinsp) {
    const Concurrent& conc = ctx.conc;
    FileLine* const flp = conc.flp;
    std::vector<const PortAssoc*> assocs;
    if (ctx.positional) {
        if (portIndex < conc.associations.size()) assocs.push_back(&conc.associations[portIndex]);
    } else {
        for (const PortAssoc& assoc : conc.associations) {
            if (assoc.formal == portp->name()) assocs.push_back(&assoc);
        }
        // A whole port may be associated once; only a part of it (a slice or a record
        // element) may be associated alongside another part
        if (assocs.size() > 1
            && std::any_of(assocs.begin(), assocs.end(), [](const PortAssoc* assocp) {
                   return !assocp->hasFormalRange && assocp->formalMember.empty();
               })) {
            flp->v3error("VHDL: port '" + assocs.front()->formalSpelling
                         + "' is associated more than once.");
            return false;
        }
    }
    const VType portType = typeOfDType(portp->dtypep());
    // The packed vector of a record port does not name its elements, so an aggregate
    // actual or default takes the record type the port is declared with. An
    // unconstrained record port takes the element ranges this instance fixed.
    VType recPortType = instanceRecordPortType(ctx.entityp, portp);
    const auto boundsIt = ctx.portBounds.find(portp->name());
    if (boundsIt != ctx.portBounds.end() && boundsIt->second.isRecord
        && recPortType.kind == VType::RECORD) {
        const PortBounds& bounds = boundsIt->second;
        for (size_t i = 0; i < bounds.memberNames.size(); ++i) {
            for (VField& field : recPortType.fields) {
                if (field.name != bounds.memberNames[i]) continue;
                setRecordField(flp, field, bounds.memberLeft[i], bounds.memberRight[i],
                               bounds.memberAscending[i]);
            }
        }
        finalizeRecordFields(flp, recPortType);
    }
    const VType& aggType = recPortType.kind == VType::RECORD ? recPortType : portType;
    AstNodeExpr* actualp = nullptr;
    if (assocs.empty()) {
        actualp = instanceDeclaredDefault(conc, ctx.entityp, portp, aggType, portType,
                                          ctx.defaultError);
        if (!actualp && !ctx.defaultError && portp->direction().isInput()) {
            flp->v3error("VHDL: input port " + portp->prettyNameQ()
                         + " has no default value and is not associated.");
            return false;
        }
    } else {
        const bool partial
            = std::any_of(assocs.begin(), assocs.end(),
                          [](const PortAssoc* assocp) { return assocp->hasFormalRange; });
        const bool recordMember
            = std::any_of(assocs.begin(), assocs.end(),
                          [](const PortAssoc* assocp) { return !assocp->formalMember.empty(); });
        if (recordMember) {
            if (!bindRecordMemberPort(ctx, portp, assocs, recPortType, actualp)) return false;
        } else if (!partial) {
            if (!bindWholePort(ctx, portp, *assocs.front(), aggType, portType, actualp)) {
                return false;
            }
        } else if (!portType.isVector() || portType.elemWidth != 1) {
            unsupportedError(flp, "part of a port that is not a bit vector");
            return false;
        } else if (portp->direction().isInout()) {
            unsupportedError(flp, "part of an inout port");
            return false;
        } else if (!bindPartialPort(ctx, portp, assocs, portType, actualp)) {
            return false;
        }
    }
    AstPin* const pinp = new AstPin{flp, portp->pinNum(), portp->name(), actualp};
    pinp->modVarp(portp);
    pinsp = pinsp ? AstNode::addNext<AstPin, AstPin>(pinsp, pinp) : pinp;
    return true;
}

//! An instantiation becomes an AstCell bound to the instantiated entity's module
AstNode* Lowering::lowerInstance(const Concurrent& conc) {
    FileLine* const flp = conc.flp;
    AstModule* modp = nullptr;
    const EntityUnit* entityp = nullptr;
    if (!lookupInstanceModule(conc, modp, entityp)) return nullptr;
    // A component declaration's interface must conform to the entity the instance binds
    if (!conc.entityInstantiation && entityp && !checkComponentConformance(conc, *entityp)) {
        return nullptr;
    }
    if (!selectInstanceArchitecture(conc, entityp, modp)) return nullptr;
    PortBoundsMap portBounds;
    if (!specializeInstancePorts(conc, entityp, modp, portBounds)) return nullptr;
    std::vector<AstVar*> ports;
    bool positional = false;
    if (!collectInstancePorts(conc, modp, ports, positional)) return nullptr;
    InstancePortBind ctx{conc, entityp, portBounds, positional, false};
    AstPin* pinsp = nullptr;
    for (size_t portIndex = 0; portIndex < ports.size(); ++portIndex) {
        if (!bindInstancePort(ctx, ports[portIndex], portIndex, pinsp)) return nullptr;
    }
    if (!positional) {
        for (const PortAssoc& assoc : conc.associations) {
            const bool known = std::any_of(ports.begin(), ports.end(), [&](const AstVar* portp) {
                return portp->name() == assoc.formal;
            });
            if (!known) {
                flp->v3error("VHDL: entity '" + conc.instanceName + "' has no port '"
                             + assoc.formalSpelling + "'.");
            }
        }
    }
    AstCell* const cellp = new AstCell{
        flp, flp, conc.instanceLabel + m_genSuffix, modp->name(), pinsp, nullptr, nullptr};
    freeInstanceBindings();
    return cellp;
}

//! Declare the own signals, variables, constants and types of a block or generate
void Lowering::emitLocalDecls(const std::vector<Decl>& decls, const std::vector<TypeDecl>& types,
                              const char* prefix) {
    // A type or subtype in the same declarative part may name a constant declared there
    preScanIntConstants(decls);
    registerTypes(types, decls);
    for (const Decl& decl : decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectUnmodelledObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            const std::string mangled
                = std::string{prefix} + std::to_string(++m_loopSeq) + "__" + name.first;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
            traceDeclared(varp);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.kind == Decl::VARIABLE) m_variables.insert(name.first);
            if (!decl.init) continue;
            AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
            AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
            if (!rhsp) {
                lhsp->deleteTree();
                continue;
            }
            if (!valueConforms(type, *decl.init, "initial value")) {
                rhsp->deleteTree();
                lhsp->deleteTree();
                continue;
            }
            rhsp = resizeExpr(rhsp, type, decl.flp);
            m_modp->addStmtsp(new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
        }
    }
    finishDeferredTypes();
}

void Lowering::emitConcurrent(const Concurrent& conc) {
    const Concurrent& stmt = conc;
    if (stmt.kind == Concurrent::ASSERT) {
        // A concurrent assertion reports on every evaluation, and failure ends the run
        AstNodeStmt* const assertp
            = assertBody(stmt.flp, *stmt.target, stmt.value.get(), stmt.severity);
        if (assertp)
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, assertp});
        return;
    }
    if (stmt.kind == Concurrent::CALL) {
        // A concurrent procedure call is one combinational process: its locals are
        // intermediates and the signal actuals are driven continuously
        ++m_concurrentProc;
        AstNodeStmt* const callp = lowerProcedureCall(*stmt.value, true);
        --m_concurrentProc;
        if (callp) {
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, callp});
        }
        return;
    }
    if (stmt.kind == Concurrent::GEN_FOR) {
        emitGenerateFor(stmt);
        return;
    }
    if (stmt.kind == Concurrent::GEN_IF) {
        // Conditions are evaluated in order; elaboration proceeds in the first
        // branch whose condition is true, or in the else part
        const GenBranch* takenp = nullptr;
        for (const GenBranch& branch : stmt.branches) {
            if (!branch.cond) {  // The else part
                takenp = &branch;
                break;
            }
            AstNodeExpr* const condp = lowerCond(*branch.cond);
            AstConst* const foldedp = condp ? foldStatic(condp) : nullptr;
            if (condp) condp->deleteTree();
            if (!foldedp) {
                unsupportedError(stmt.flp, "generate condition that is not static");
                return;
            }
            const bool taken = foldedp->num().isTrue();
            foldedp->deleteTree();
            if (taken) {
                takenp = &branch;
                break;
            }
        }
        if (!takenp) return;  // No condition was true and there is no else part
        const ScopeSave saved = saveScope();
        const std::string savedSuffix = m_genSuffix;
        m_genSuffix = savedSuffix + "__gen" + std::to_string(++m_genSeq);
        emitLocalDecls(takenp->decls, takenp->types, "__Vgen_");
        registerComponents(takenp->components);
        registerSubprograms(takenp->subprograms, -1);
        for (const auto& bodyConcp : takenp->stmts) {
            if (bodyConcp) emitConcurrent(*bodyConcp);
        }
        m_genSuffix = savedSuffix;
        restoreScope(saved);
        return;
    }
    if (stmt.kind == Concurrent::BLOCK) {
        // A block's declarative items are visible to its statements only
        const ScopeSave saved = saveScope();
        emitLocalDecls(stmt.decls, stmt.types, "__Vblk_");
        registerComponents(stmt.components);
        registerSubprograms(stmt.subprograms, -1);
        for (const auto& bodyConcp : stmt.body) {
            if (bodyConcp) emitConcurrent(*bodyConcp);
        }
        restoreScope(saved);
        return;
    }
    if (stmt.kind == Concurrent::INSTANCE) {
        if (AstNode* const cellp = lowerInstance(stmt)) m_modp->addStmtsp(cellp);
        return;
    }
    if (stmt.kind == Concurrent::PROCESS) {
        lowerProcess(*stmt.process);
        return;
    }
    emitConcurrentAssign(stmt);
}

void Lowering::emitGenerateFor(const Concurrent& stmt) {
    // A generate loop is unrolled with its parameter bound as a constant
    int64_t left = 0;
    int64_t right = 0;
    bool up = stmt.rangeAscending;
    // `for i in obj'range generate` iterates the object's own bounds, which come from
    // its type just as a `prefix'range` slice does
    const Expr* const rangep
        = !stmt.hi && stmt.lo && stmt.lo->kind == Expr::ATTR
                  && (stmt.lo->text == "range" || stmt.lo->text == "reverse_range")
              ? stmt.lo.get()
              : nullptr;
    if (rangep) {
        if (!rangep->a || rangep->a->kind != Expr::NAME) {
            unsupportedError(stmt.flp, "generate over 'range of a non-name prefix");
            return;
        }
        const VType* const rangeTypep = typeOfName(*rangep->a);
        if (!rangeTypep) {
            unsupportedError(stmt.flp, "generate over 'range of an unknown name");
            return;
        }
        const bool reverse = rangep->text == "reverse_range";
        left = reverse ? rangeTypep->right : rangeTypep->left;
        right = reverse ? rangeTypep->left : rangeTypep->right;
        up = reverse ? !rangeTypep->ascending : rangeTypep->ascending;
    } else if (!stmt.hi && stmt.lo && stmt.lo->kind == Expr::NAME) {
        // `for i in <subtype> generate` iterates the subtype's own bounds
        // (IEEE 1076-2008 11.8), as a loop over a discrete subtype does
        const auto subtypeIt = m_types.find(stmt.lo->text);
        if (subtypeIt == m_types.end() || subtypeIt->second.kind != VType::INT) {
            unsupportedError(stmt.flp, "generate over a name that is not a subtype");
            return;
        }
        left = subtypeIt->second.intLow;
        right = subtypeIt->second.intHigh;
        up = left <= right;
    } else if (!staticInt(stmt.lo, left) || !staticInt(stmt.hi, right)) {
        unsupportedError(stmt.flp, "generate range that is not static");
        return;
    }
    if (up ? right < left : right > left) return;  // Empty range
    if (std::llabs(right - left) > MAX_FOLD_ITERATIONS) {
        unsupportedError(stmt.flp, "generate range with more than "
                                       + std::to_string(MAX_FOLD_ITERATIONS) + " iterations");
        return;
    }
    const auto saved = m_consts.find(stmt.loopVar);
    const bool hadConst = saved != m_consts.end();
    const int64_t savedValue = hadConst ? saved->second : 0;
    for (int64_t value = left;; value += up ? 1 : -1) {
        const ScopeSave saved = saveScope();
        const std::string savedSuffix = m_genSuffix;
        m_genSuffix = savedSuffix + "__gen" + std::to_string(++m_genSeq);
        m_consts[stmt.loopVar] = value;
        emitLocalDecls(stmt.decls, stmt.types, "__Vgen_");
        registerComponents(stmt.components);
        registerSubprograms(stmt.subprograms, -1);
        for (const auto& bodyConcp : stmt.body) {
            if (bodyConcp) emitConcurrent(*bodyConcp);
        }
        m_genSuffix = savedSuffix;
        restoreScope(saved);
        m_consts[stmt.loopVar] = value;
        if (value == right) break;
    }
    if (hadConst) {
        m_consts[stmt.loopVar] = savedValue;
    } else {
        m_consts.erase(stmt.loopVar);
    }
}

//! One concurrent assignment, with the statements an inlined call's locals produced
void Lowering::emitConcurrentAssign(const Concurrent& stmt) {
    // The assignment body is a statement list, so an inlined function's array local can
    // be a real variable here; a failure leaves statements behind, so they are dropped
    struct MaterializeScope final {
        Lowering* const selfp;  // Lowering whose flag is set
        const bool saved;  // Value of m_materializeLocals to restore
        explicit MaterializeScope(Lowering* lowerp)
            : selfp{lowerp}
            , saved{lowerp->m_materializeLocals} {
            selfp->m_materializeLocals = true;
        }
        ~MaterializeScope() {
            selfp->m_materializeLocals = saved;
            selfp->discardPendingStmts();
        }
    } const scope{this};
    if (stmt.kind == Concurrent::SEL_ASSIGN) {
        AstNodeStmt* const casep = lowerSelected(stmt);
        if (!casep) return;
        if (AstNodeStmt* const allp = takePendingFor(casep)) {
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, allp});
        }
        return;
    }
    if (stmt.target->kind == Expr::AGG_POSITIONAL || stmt.target->kind == Expr::AGG_NAMED) {
        if (stmt.kind != Concurrent::ASSIGN) {
            unsupportedError(stmt.flp, "aggregate target in a conditional or selected assignment");
            return;
        }
        AstNodeStmt* const bodyp
            = lowerAggregateAssign(stmt.flp, *stmt.target, *stmt.value, false, true);
        if (AstNodeStmt* const allp = takePendingFor(bodyp)) {
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, allp});
        }
        return;
    }
    AstNodeExpr* const lhsp = lowerDesignator(*stmt.target, true);
    if (!lhsp) return;
    const VType ltype = typeOfTarget(*stmt.target, lhsp);
    // A whole null array is a no-op, and a zero-width assignment fails in V3Width
    if (ltype.isVector() && ltype.width == 0) {
        lhsp->deleteTree();
        return;
    }
    if (stmt.kind == Concurrent::COND_ASSIGN && concHasEdge(stmt)) {
        emitClockedConcurrentAssign(stmt, ltype, lhsp);
        return;
    }
    const bool holds = !stmt.alternatives.empty()
                       && (stmt.alternatives.back().second
                           || std::any_of(stmt.alternatives.begin(), stmt.alternatives.end(),
                                          [](const std::pair<ExprP, ExprP>& alt) {
                                              return alt.first->kind == Expr::UNAFFECTED;
                                          }));
    if (stmt.kind == Concurrent::COND_ASSIGN && holds) {
        // A missing else or an unaffected branch makes the target hold its value
        AstNodeStmt* const bodyp = lowerLatchedAlternatives(stmt, ltype, lhsp);
        lhsp->deleteTree();
        if (AstNodeStmt* const allp = takePendingFor(bodyp)) {
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS_LATCH, nullptr, allp});
        }
        return;
    }
    AstNodeExpr* rhsp = nullptr;
    if (stmt.kind == Concurrent::COND_ASSIGN) {
        rhsp = lowerAlternatives(stmt, ltype);
    } else {
        rhsp = lowerExpr(*stmt.value, &ltype);
        if (rhsp && stmt.value && !valueConforms(ltype, *stmt.value, "assignment value")) {
            rhsp->deleteTree();
            rhsp = nullptr;
        }
        if (rhsp) rhsp = resizeExpr(rhsp, ltype, stmt.flp);
    }
    if (!rhsp) {
        lhsp->deleteTree();
        return;
    }
    if (!stmt.waveform.empty()) {
        emitConcurrentWaveform(stmt, ltype, lhsp, rhsp);
        return;
    }
    if (stmt.delay && !staticZeroDelay(stmt.delay)) {
        // A concurrent delayed assignment repeats: it waits, then drives the target
        AstNodeStmt* const assignp = new AstAssign{stmt.flp, lhsp, rhsp};
        AstNodeStmt* const delayp = lowerDelay(*stmt.delay, stmt.flp);
        if (!delayp) {
            assignp->deleteTree();
            return;
        }
        AstNodeStmt* const bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(delayp, assignp);
        m_modp->addStmtsp(
            new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS, nullptr, takePendingFor(bodyp)});
        return;
    }
    m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                                    takePendingFor(new AstAssignW{stmt.flp, lhsp, rhsp})});
}

void Lowering::emitClockedConcurrentAssign(const Concurrent& stmt, const VType& ltype,
                                           AstNodeExpr* lhsp) {
    // Register clocked by the edge the condition names
    m_edges.clear();
    m_initial = false;
    // Names tested before the clocked branch, e.g. an asynchronous reset
    std::set<std::string> levelNames;
    for (const auto& alt : stmt.alternatives) {
        if (alt.second && containsEdgeCall(*alt.second)) break;
        if (alt.second) collectExprNames(*alt.second, levelNames);
    }
    AstNodeStmt* const bodyp = lowerClockedAlternatives(stmt, ltype, lhsp);
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    AstSenItem* itemsp = nullptr;
    for (const Edge& edge : edges) {
        AstSenItem* const itemp = new AstSenItem{
            stmt.flp, edge.type, new AstVarRef{stmt.flp, edge.varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    for (const std::string& name : levelNames) {
        const auto it = m_scope.find(name);
        if (it == m_scope.end()) continue;
        if (std::any_of(edges.begin(), edges.end(),
                        [&](const Edge& edge) { return edge.varp == it->second.varp; })) {
            continue;
        }
        AstSenItem* const itemp
            = new AstSenItem{stmt.flp, VEdgeType::ET_CHANGED,
                             new AstVarRef{stmt.flp, it->second.varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    if (!bodyp || !itemsp) {
        if (bodyp) bodyp->deleteTree();
        if (itemsp) itemsp->deleteTree();
        lhsp->deleteTree();
        return;
    }
    m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS, new AstSenTree{stmt.flp, itemsp},
                                    takePendingFor(bodyp)});
    lhsp->deleteTree();
}

void Lowering::emitConcurrentWaveform(const Concurrent& stmt, const VType& ltype,
                                      AstNodeExpr* lhsp, AstNodeExpr* rhsp) {
    // A waveform list repeats: each element after the first waits its own delay, and
    // the last element's delay closes the period (IEEE 1076-2008 10.5)
    AstNodeStmt* bodyp = new AstAssign{stmt.flp, lhsp, rhsp};
    const Expr* lastDelayp = stmt.delay.get();
    for (const auto& element : stmt.waveform) {
        if (element.second) {
            AstNodeStmt* const delayp = lowerDelay(*element.second, stmt.flp);
            if (!delayp) {
                bodyp->deleteTree();
                return;
            }
            bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, delayp);
            lastDelayp = element.second.get();
        }
        AstNodeExpr* const elementp = lowerExpr(*element.first, &ltype);
        if (!elementp) {
            bodyp->deleteTree();
            return;
        }
        bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(
            bodyp, new AstAssign{stmt.flp, lhsp->cloneTree(false),
                                 resizeExpr(elementp, ltype, stmt.flp)});
    }
    if (lastDelayp) {
        AstNodeStmt* const delayp = lowerDelay(*lastDelayp, stmt.flp);
        if (!delayp) {
            bodyp->deleteTree();
            return;
        }
        bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, delayp);
    }
    m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS, nullptr, takePendingFor(bodyp)});
}

//######################################################################
// Entry point

//! Elaborate a generic-package instance into the table under the instance name.

}  // namespace vhdlnative
