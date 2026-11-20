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

#include "V3Error.h"
#include "V3FileLine.h"
#include "V3VhdlNativeLower.h"
#include "V3VhdlNativeLowerImpl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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

void Lowering::preScanIntConstants(const std::vector<Decl>& decls) {
    for (const Decl& decl : decls) {
        if (decl.kind != Decl::CONSTANT || !decl.init) continue;
        const std::string& mark = decl.type.mark;
        const bool intMark = mark == "integer" || mark == "natural" || mark == "positive";
        if (!intMark) {
            const auto typeIt = m_types.find(mark);
            if (typeIt == m_types.end() || typeIt->second.kind != VType::INT) continue;
        }
        int64_t value = 0;
        if (!evalInt(*decl.init, value)) continue;
        for (const auto& name : decl.names) m_preConsts[name.first] = value;
    }
}

void Lowering::emitConstant(const std::vector<std::pair<std::string, std::string>>& names,
                            const TypeSpec& type, const Expr* initp, FileLine* flp,
                            bool deferredOk) {
    if (!initp) {
        // A package body completes the constants its specification deferred
        if (!deferredOk) flp->v3error("VHDL: constant without a value");
        return;
    }
    // A call whose body this pass cannot read: the body's pass completes the constant
    // rather than reporting the call (IEEE 1800-2023 6.6.1). The specifications are
    // emitted before any body, so a call to a function another package defines in its
    // body has none yet. A name with arguments parses as an indexed name and becomes a
    // call once the prefix names a subprogram.
    if (deferredOk) {
        std::string callName;
        const Expr* namep = nullptr;
        if (initp->kind == Expr::CALL) {
            callName = initp->text;
            namep = initp;
        } else if (initp->kind == Expr::INDEX && initp->a) {
            callName = initp->a->text;
            namep = initp->a.get();
        } else if (initp->kind == Expr::NAME) {
            // A function may be called by naming it alone when its formals have defaults
            callName = initp->text;
            namep = initp;
        }
        if (!callName.empty()) {
            // A call qualified by a package name imports it, as resolving the call does
            std::string packageName;
            if (namep && namep->kind == Expr::SELECTED && namep->a
                && packageNameOf(*namep->a, packageName)) {
                importPackage(packageName);
            }
            const auto refs = visibleSubprograms(callName);
            const bool anyBody
                = std::any_of(refs.begin(), refs.end(),
                              [](const SubprogramDecl* const subp) { return subp->hasBody; });
            if (!refs.empty() && !anyBody) {
                m_deferredConsts.push_back(DeferredConst{names, &type, initp, flp, m_unitName});
                return;
            }
        }
    }
    VType constType;
    bool intConstant = true;
    bool realConstant = false;
    AstNodeExpr* valuep = nullptr;  // A value lowered early, when the type needed it
    if (isLiteralSizedVector(type, initp)) {
        // The mark leaves the width open, so the literal fixes it
        constType = literalVectorType(flp, type.mark, static_cast<int>(initp->bits.size()));
        intConstant = false;
    } else if (initp->kind == Expr::STRING_LIT && type.mark == "string" && !type.hasRange
               && type.constraintMark.empty()) {
        // An unconstrained string takes its bounds from the literal, as a vector does
        constType = stringType(flp, static_cast<int>(initp->spelling.size()));
        intConstant = false;
    } else if (type.mark == "string" && !type.hasRange && type.constraintMark.empty()) {
        // An unconstrained string takes its bounds from a static expression too, such as
        // the image of a value, so the value is folded before the type is fixed
        valuep = lowerExpr(*initp, nullptr);
        if (!valuep) return;
        if (AstConst* const foldedp = foldStatic(valuep)) {
            if (foldedp != valuep) valuep->deleteTree();
            valuep = foldedp;
        }
        constType = stringType(flp, valuep->width() / 8);
        intConstant = false;
    } else {
        // A record constant whose type leaves a member unconstrained takes that member's
        // bounds from the initial value, as a vector does
        TypeSpec declaredType = copySpec(type);
        if (initp) completeRecordType(declaredType, *initp);
        if (declaredType.flp && resolveType(declaredType, constType)) {
            intConstant = constType.isInt();
            realConstant = constType.kind == VType::REAL;
        }
        // An array whose element type leaves a member unconstrained has no width until its
        // aggregate fixes one, which is not modelled
        if (!constType.dtp && constType.kind == VType::ARRAY
            && (!constType.elemp || constType.elemp->width == 0)) {
            unsupportedError(flp, "constant of an array of unconstrained elements");
            return;
        }
        // An unconstrained array constant whose initializer is a value rather than an
        // aggregate takes its length from that value, as a function returning an
        // unconstrained array constrains it: poc's `RAM_Type_Depth` is
        // `integer_vector := get_ram_type(...)` (IEEE 1076-2008 5.3.2)
        if (!constType.dtp && constType.kind == VType::ARRAY && initp
            && initp->kind != Expr::AGG_POSITIONAL && initp->kind != Expr::AGG_NAMED
            && !type.hasRange && type.constraintMark.empty() && type.indexConstraints.empty()
            && constType.elemWidth > 0) {
            valuep = lowerExpr(*initp, nullptr);
            if (!valuep) return;
            if (AstConst* const foldedp = foldStatic(valuep)) {
                if (foldedp != valuep) valuep->deleteTree();
                valuep = foldedp;
            }
            const int length = valuep->width() / constType.elemWidth;
            if (length > 0) {
                TypeSpec bounded = copySpec(type);
                bounded.hasRange = true;
                bounded.ascending = true;
                bounded.left = std::unique_ptr<Expr>(new Expr{Expr::INT_LIT});
                bounded.left->flp = flp;
                bounded.left->intValue = constType.intLow;
                bounded.right = std::unique_ptr<Expr>(new Expr{Expr::INT_LIT});
                bounded.right->flp = flp;
                bounded.right->intValue = constType.intLow + length - 1;
                if (resolveType(bounded, constType)) {
                    intConstant = constType.isInt();
                    realConstant = constType.kind == VType::REAL;
                }
            }
        }
        // An unconstrained array constant takes its bounds from its aggregate, one element
        // per aggregate element from the index subtype's low bound (IEEE 1076-2008 9.3.3.2)
        if (!constType.dtp && constType.kind == VType::ARRAY && initp
            && initp->kind == Expr::AGG_POSITIONAL && !initp->args.empty() && !type.hasRange
            && type.constraintMark.empty() && type.indexConstraints.empty()) {
            TypeSpec bounded = copySpec(type);
            bounded.hasRange = true;
            bounded.ascending = true;
            bounded.left = std::unique_ptr<Expr>(new Expr{Expr::INT_LIT});
            bounded.left->flp = flp;
            bounded.left->intValue = constType.intLow;
            bounded.right = std::unique_ptr<Expr>(new Expr{Expr::INT_LIT});
            bounded.right->flp = flp;
            bounded.right->intValue
                = constType.intLow + static_cast<int64_t>(initp->args.size()) - 1;
            if (resolveType(bounded, constType)) {
                intConstant = constType.isInt();
                realConstant = constType.kind == VType::REAL;
            }
        }
    }
    if (initp && !valueConforms(constType, *initp, "constant value")) return;
    // A packed constant of that many bits is copied once per write into it, so a memory
    // image is reported rather than folded
    if (constType.width > (1 << 20)) {
        unsupportedError(flp, "constant value that is too wide to fold");
        return;
    }
    // A real survives only as an elaboration-time constant: fold it to a value that
    // `evalReal` and real comparisons consume, so no floating type reaches the model.
    if (realConstant) {
        double realVal = 0.0;
        if (!evalReal(*initp, realVal)) {
            unsupportedError(flp, "real constant value that is not static");
            return;
        }
        for (const auto& name : names) {
            if (!reserveName(name.first, name.second, flp)) continue;
            m_realConsts[name.first] = realVal;
        }
        return;
    }
    if (intConstant) {
        int64_t value = 0;
        if (evalInt(*initp, value)) {
            // IEEE 1076-2008 5.4: the value is within its declared subtype's bounds. Only
            // a subtype of a predefined integer type carries bounds that are complete here
            // (a physical type is modelled as an integer too, without them).
            const auto subtypeIt = m_types.find(type.mark);
            const bool integerSubtype = subtypeIt != m_types.end()
                                        && subtypeIt->second.kind == VType::INT
                                        && isPredefinedIntegerMark(subtypeIt->second.typeName);
            if (integerSubtype
                && (value < subtypeIt->second.intLow || value > subtypeIt->second.intHigh)) {
                flp->v3error("VHDL: constant value is out of the range of subtype '" + type.mark
                             + "' (IEEE 1076-2008 5.4)");
                return;
            }
            for (const auto& name : names) {
                if (!reserveName(name.first, name.second, flp)) continue;
                m_consts[name.first] = value;
                m_constTypes[name.first] = constType;
            }
            return;
        }
        // A call to a declared function is static as well: lower and fold it below
        if (!constType.dtp) constType = intType(flp);
    }
    if (!constType.dtp) {
        // An unconstrained array of records whose elements leave a member unconstrained
        // has no width until its aggregate supplies one
        unsupportedError(flp, "constant of an unresolved type");
        if (valuep) valuep->deleteTree();
        return;
    }
    if (!valuep) valuep = lowerExpr(*initp, &constType);
    if (!valuep) return;
    if (AstConst* const foldedp = foldStatic(valuep)) {
        valuep->deleteTree();
        valuep = foldedp;
    }
    AstConst* const constp = VN_CAST(valuep, Const);
    if (!constp) {
        unsupportedError(flp, "constant value that is not static");
        valuep->deleteTree();
        return;
    }
    // IEEE 1076-2008 9.3.2: an explicit value must have the declared type's length. The
    // width is not visible to valueConforms for a literal or a simple designator; an
    // operator expression takes its width from the context instead.
    const bool sized = initp->kind == Expr::STRING_LIT || initp->kind == Expr::BITS_LIT
                       || initp->kind == Expr::NAME || initp->kind == Expr::SELECTED
                       || initp->kind == Expr::SLICE;
    if (sized && constType.width > 0 && valuep->width() != constType.width) {
        flp->v3error("VHDL: constant value does not have the type's length");
        valuep->deleteTree();
        return;
    }
    constp->dtypep(constType.dtp);
    for (size_t i = 0; i < names.size(); ++i) {
        const auto& name = names[i];
        if (!reserveName(name.first, name.second, flp)) continue;
        const bool last = i + 1 == names.size();
        m_ownedConsts.push_back(
            std::unique_ptr<AstConst>(last ? constp : constp->cloneTree(false)));
        m_constValues[name.first] = m_ownedConsts.back().get();
        m_constTypes[name.first] = constType;
        // A string value's text is what a string generic or a comparison needs, and a
        // constant can be bound to a generic just as a generic can be forwarded
        if (constType.kind == VType::ARRAY && constType.elemWidth == 8) {
            m_stringConsts[name.first] = stringTextOfConst(m_ownedConsts.back().get());
        }
    }
}

//! Register the entity's generics as constants; bindings hold the bound actuals and
//! the rest fall back to their defaults. A probe binds only what a range needs.
//! An unconstrained vector generic: its bounds come from the actual at each instance
bool unconstrainedVectorSpec(const TypeSpec& spec) {
    const std::string& mark = spec.mark;
    const bool vectorMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                            || mark == "bit_vector" || mark == "unsigned" || mark == "signed"
                            || mark == "u_unsigned" || mark == "u_signed";
    return vectorMark && !spec.hasRange && spec.constraintMark.empty()
           && spec.indexConstraints.empty() && spec.memberConstraints.empty();
}

void Lowering::instantiatePackage(const std::vector<PackageUnit>& packages,
                                  const PackageInstance& inst, PackageTable::Impl& table) {
    const PackageUnit* specp = nullptr;
    const PackageUnit* bodyp = nullptr;
    for (const PackageUnit& pkg : packages) {
        if (pkg.name != inst.baseName) continue;
        if (pkg.isBody)
            bodyp = &pkg;
        else
            specp = &pkg;
    }
    if (!specp) {
        unsupportedError(inst.flp, "package instantiation of unknown '" + inst.baseName + "'");
        return;
    }
    GenericBindings bindings;
    const auto freeBindings = [](GenericBindings& bs) {
        for (auto& entry : bs) {
            if (entry.second.valuep) entry.second.valuep->deleteTree();
        }
    };
    // Flatten the generics so a positional map can be matched by declaration order
    struct Item final {
        const GenericDecl* declp;
        std::string folded;
    };
    std::vector<Item> items;
    for (const GenericDecl& gen : specp->generics)
        for (const auto& name : gen.names) items.push_back({&gen, name.first});
    const std::vector<PortAssoc>& assocs = inst.genericAssociations;
    const bool positional = std::any_of(assocs.begin(), assocs.end(),
                                        [](const PortAssoc& a) { return a.formal.empty(); });
    for (size_t k = 0; k < items.size(); ++k) {
        const GenericDecl& gen = *items[k].declp;
        const PortAssoc* assocp = nullptr;
        if (positional) {
            if (k < assocs.size()) assocp = &assocs[k];
        } else {
            for (const PortAssoc& a : assocs)
                if (a.formal == items[k].folded) assocp = &a;
        }
        if (!assocp) {
            if (!gen.init) {
                inst.flp->v3error("VHDL: package generic '" + items[k].folded + "' of '"
                                  + inst.baseName + "' is not bound in instance '" + inst.spelling
                                  + "'");
                freeBindings(bindings);
                return;
            }
            continue;  // The generic keeps its own default value
        }
        if (!assocp->actual) {
            assocp->flp->v3error("VHDL: 'open' is not allowed on a package generic association");
            freeBindings(bindings);
            return;
        }
        if (gen.isSubprogram) {
            // Packages are lowered without an instantiation scope, so the actual subprogram
            // (which lives in the caller) cannot be resolved here
            unsupportedError(assocp->flp, "generic subprogram parameter on a package");
            freeBindings(bindings);
            return;
        }
        GenericBinding binding;
        if (gen.isType) {
            binding.isType = true;
            if (!resolveTypeActual(*assocp->actual, binding.typeValue)) {
                unsupportedError(assocp->flp, "generic type actual");
                freeBindings(bindings);
                return;
            }
        } else if (!evalInt(*assocp->actual, binding.intValue)) {
            unsupportedError(assocp->flp, "package generic actual that is not static");
            freeBindings(bindings);
            return;
        } else {
            binding.isInt = true;
        }
        bindings[items[k].folded] = binding;
    }
    // Publish the spec and body under the instance name, with the generics bound
    emitPackage(*specp, table, &bindings, inst.name);
    if (bodyp) emitPackage(*bodyp, table, &bindings, inst.name);
    freeBindings(bindings);
}

}  // namespace vhdlnative
