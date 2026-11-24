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
// Elaborates VHDL subprogram bodies and inlines their calls
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

void Lowering::setScope(const ScopeSave& saved) {
    m_scope = saved.scope;
    m_consts = saved.consts;
    m_types = saved.types;
    m_ambiguousTypes = saved.ambiguousTypes;
    m_ambiguousLiterals = saved.ambiguousLiterals;
    m_constValues = saved.constValues;
    m_constTypes = saved.constTypes;
    m_enumConsts = saved.enumConsts;
    m_subprograms = saved.subprograms;
    m_components = saved.components;
    m_stdUses = saved.stdUses;
}

//! Whether two declarations are one profile: a declaration of the same name and formal
//! types is a homograph of the other (IEEE 1076-2008 4.5.2, 12.4)
bool homograph(const SubprogramDecl& lhs, const SubprogramDecl& rhs) {
    if (lhs.kind != rhs.kind || lhs.formals.size() != rhs.formals.size()) return false;
    if (lhs.returnType.mark != rhs.returnType.mark
        || lhs.returnType.objectName != rhs.returnType.objectName) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.formals.size(); ++i) {
        const FormalDecl& lf = lhs.formals[i];
        const FormalDecl& rf = rhs.formals[i];
        if (lf.names.size() != rf.names.size() || lf.mode != rf.mode) return false;
        for (std::size_t n = 0; n < lf.names.size(); ++n) {
            if (lf.names[n].first != rf.names[n].first) return false;
        }
        // The type mark decides, not the constraint written after it
        if (lf.type.mark != rf.type.mark || lf.type.objectName != rf.type.objectName
            || lf.type.elementMark != rf.type.elementMark
            || lf.type.constraintMark != rf.type.constraintMark) {
            return false;
        }
    }
    return true;
}

void Lowering::registerComponents(const std::vector<ComponentDecl>& components) {
    for (const ComponentDecl& component : components) {
        m_components[component.name] = &component;
    }
}

void Lowering::registerSubprograms(const std::vector<SubprogramDecl>& decls, int frame,
                                   bool processLocal) {
    for (const SubprogramDecl& decl : decls) {
        m_imported.erase(decl.name);  // The local declaration shadows the package member
        std::vector<SubprogramRef>& refs = m_subprograms[decl.name];
        // A specification and its completing body are one subprogram; two bodies of one
        // declarative region are not, while an inner region hides the outer one
        if (decl.hasBody && std::any_of(refs.begin(), refs.end(), [&](const SubprogramRef& ref) {
                return !ref.imported && ref.declp->hasBody && ref.processLocal == processLocal
                       && ref.frame == frame && homograph(*ref.declp, decl);
            })) {
            decl.flp->v3error("VHDL: duplicate subprogram body for '" + decl.spelling + "'.");
            continue;
        }
        // A declaration of this unit hides an imported one of the same profile; the
        // declaration a package body completes is its own, not one it hides
        // (IEEE 1076-2008 12.4)
        refs.erase(std::remove_if(refs.begin(), refs.end(),
                                  [&](const SubprogramRef& ref) {
                                      return ref.imported && !ref.ownSpec
                                             && homograph(*ref.declp, decl);
                                  }),
                   refs.end());
        refs.push_back(SubprogramRef{&decl, processLocal, frame});
    }
}

std::vector<const SubprogramDecl*> Lowering::visibleSubprograms(const std::string& name) const {
    std::vector<const SubprogramDecl*> out;
    const auto it = m_subprograms.find(name);
    if (it == m_subprograms.end()) return out;
    // Declarations of a nested inlining hide the outer ones, then a process's own
    // subprograms hide those of the declarative part and packages
    int bestFrame = -2;
    bool bestProcessLocal = false;
    for (const SubprogramRef& ref : it->second) {
        if (ref.frame > bestFrame
            || (ref.frame == bestFrame && ref.processLocal && !bestProcessLocal)) {
            bestFrame = ref.frame;
            bestProcessLocal = ref.processLocal;
        }
    }
    for (const SubprogramRef& ref : it->second) {
        if (ref.frame != bestFrame || ref.processLocal != bestProcessLocal) continue;
        out.push_back(ref.declp);
    }
    return out;
}

//! Whether a subprogram opens a file to build its result while the design is elaborated
bool readsFile(const SubprogramDecl& subp) {
    return std::any_of(subp.decls.begin(), subp.decls.end(),
                       [](const Decl& decl) { return decl.kind == Decl::FILE && decl.init; });
}

bool Lowering::subprogramMatches(const Expr& call, const SubprogramDecl& sub,
                                 std::string& why) const {
    // A formal declaration can declare several names; positional actuals fill them in order
    const std::vector<std::pair<const FormalDecl*, std::string>> slots = formalSlots(sub);
    std::vector<bool> given(slots.size(), false);
    std::size_t positional = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        if (i >= call.formalNames.size() || call.formalNames[i].empty()) {
            ++positional;
            continue;
        }
        bool found = false;
        for (std::size_t s = 0; s < slots.size(); ++s) {
            if (slots[s].second != call.formalNames[i]) continue;
            if (given[s]) {
                why = "formal '" + call.formalNames[i] + "' is given twice";
                return false;
            }
            given[s] = true;
            found = true;
            break;
        }
        if (!found) {
            why = "no formal named '" + call.formalNames[i] + "'";
            return false;
        }
    }
    if (positional > slots.size()) {
        why = "more actuals than formals";
        return false;
    }
    for (std::size_t s = 0; s < slots.size(); ++s) {
        if (s < positional) {
            if (given[s]) {
                why = "formal '" + slots[s].second + "' is given twice";
                return false;
            }
            continue;
        }
        if (given[s]) continue;
        if (!slots[s].first->init) {
            why = "no actual for formal '" + slots[s].second + "'";
            return false;
        }
    }
    return true;
}

const SubprogramDecl* Lowering::resolveCall(const Expr& call, bool function,
                                            const SubprogramRef** foundpp, const VType* contextp) {
    std::string name;
    std::vector<const SubprogramRef*> refs;
    std::string packageName;
    if (call.a && call.a->kind == Expr::SELECTED && call.a->a
        && packageNameOf(*call.a->a, packageName)) {
        // A qualified call `package.name(...)`, or `work.package.name(...)`: consult
        // that package
        name = call.a->text;
        importPackage(packageName);
        const auto it = m_subprograms.find(name);
        if (it != m_subprograms.end())
            for (const SubprogramRef& ref : it->second) refs.push_back(&ref);
    } else {
        name = call.kind == Expr::INDEX && call.a ? call.a->text : call.text;
        const auto it = m_subprograms.find(name);
        if (it != m_subprograms.end()) {
            for (const SubprogramDecl* const declp : visibleSubprograms(name)) {
                for (const SubprogramRef& ref : it->second)
                    if (ref.declp == declp) refs.push_back(&ref);
            }
        }
    }
    std::string firstWhy;
    bool withoutBody = false;
    bool sawOtherClass = false;
    std::vector<const SubprogramRef*> matching;
    for (const SubprogramRef* const refp : refs) {
        const SubprogramDecl& sub = *refp->declp;
        if ((sub.kind == SubprogramDecl::FUNCTION) != function) {
            sawOtherClass = true;
            continue;
        }
        if (!sub.hasBody) {
            withoutBody = true;
            continue;
        }
        std::string why;
        if (!subprogramMatches(call, sub, why)) {
            if (firstWhy.empty()) firstWhy = why;
            continue;
        }
        matching.push_back(refp);
    }
    if (matching.size() > 1) {
        // Several declarations take this many actuals, so the actuals' categories decide
        // which one is called (IEEE 1076-2008 12.5). A single declaration is not a
        // choice and keeps the acceptance `subprogramMatches` gave it.
        std::vector<const SubprogramRef*> fitting;
        for (const SubprogramRef* const refp : matching) {
            if (formalsAccept(call, *refp->declp)) fitting.push_back(refp);
        }
        if (fitting.empty()) {
            unsupportedError(call.flp, "no declaration of '" + name + "' accepts the actuals");
            return nullptr;
        }
        if (fitting.size() > 1) {
            std::vector<const SubprogramRef*> exact;
            for (const SubprogramRef* const refp : fitting) {
                if (formalsExact(call, *refp->declp)) exact.push_back(refp);
            }
            if (exact.size() == 1) {
                fitting = exact;
            } else if (exact.size() > 1) {
                call.flp->v3error("VHDL: ambiguous call to '" + name + "'.");
                return nullptr;
            }
        }
        if (fitting.size() > 1 && contextp) {
            // The result's context picks the declaration whose base type the enclosing
            // expression needs: an operand of a typed operator takes that one type
            // (IEEE 1076-2008 9.1), as does the actual of a typed formal
            std::vector<const SubprogramRef*> contextual;
            for (const SubprogramRef* const refp : fitting) {
                VType retType;
                if (!formalType(refp->declp->returnType, retType)) continue;
                if (typesMatch(retType, *contextp)) contextual.push_back(refp);
            }
            if (!contextual.empty()) fitting = std::move(contextual);
        }
        matching = std::move(fitting);
    } else if (matching.size() == 1 && !formalsAccept(call, *matching[0]->declp)) {
        // A single declaration is not a choice, but it still has to accept the actuals:
        // an actual of the wrong base type is a user error, not an overload ambiguity
        call.flp->v3error("VHDL: arguments to '" + name
                          + "' do not conform to its formal parameters.");
        return nullptr;
    }
    if (!matching.empty()) {
        if (foundpp) *foundpp = matching[0];
        return matching[0]->declp;
    }
    if (withoutBody) {
        unsupportedError(call.flp, "call to subprogram '" + name + "' whose body was not read");
    } else if (!refs.empty() && !firstWhy.empty()) {
        unsupportedError(call.flp,
                         "arguments to '" + name + "' match no declaration: " + firstWhy);
    } else if (!refs.empty() && sawOtherClass) {
        call.flp->v3error(
            "VHDL: '" + name + "' designates a "
            + (function ? "procedure, not a function." : "function, not a procedure."));
    }
    return nullptr;
}

//! A type mark that has no width until a constraint or context gives it one
bool isUnconstrainedMark(const std::string& mark) {
    return mark == "std_logic_vector" || mark == "std_ulogic_vector" || mark == "bit_vector"
           || mark == "unsigned" || mark == "signed" || mark == "string";
}

//! A vector or an array: one category for overload matching, as only the element type
//! separates them and that is not compared here
bool isVectorKind(const VType::Kind kind) { return kind == VType::VECTOR || kind == VType::ARRAY; }

//! The formal slots of a declaration, one per formal name, in the order positional actuals
//! fill them
std::vector<std::pair<const FormalDecl*, std::string>>
Lowering::formalSlots(const SubprogramDecl& sub) {
    std::vector<std::pair<const FormalDecl*, std::string>> slots;
    for (const FormalDecl& formal : sub.formals)
        for (const auto& name : formal.names) slots.emplace_back(&formal, name.first);
    return slots;
}

//! The category a type mark names, without resolving a constraint: an unconstrained formal
//! array has none to resolve to. False for a mark the frontend gives no category.
bool Lowering::markKind(const std::string& mark, VType::Kind& out) const {
    const auto declared = m_types.find(mark);
    if (declared != m_types.end()) {
        out = declared->second.kind;
        return true;
    }
    if (mark == "integer" || mark == "natural" || mark == "positive") {
        out = VType::INT;
        return true;
    }
    if (mark == "real") {
        out = VType::REAL;
        return true;
    }
    if (mark == "text" || mark == "line") {
        out = mark == "line" ? VType::LINE : VType::FILE;
        return true;
    }
    if (mark == "boolean" || mark == "std_logic" || mark == "std_ulogic") {
        out = VType::LOGIC;
        return true;
    }
    if (mark == "bit") {
        out = VType::BIT;
        return true;
    }
    if (isUnconstrainedMark(mark) || mark == "u_unsigned" || mark == "u_signed") {
        out = VType::VECTOR;
        return true;
    }
    return false;
}

//! The category an actual presents to a formal: what its literal, or what the static tables
//! know of the name. False when nothing is known, and an actual of unknown category fits any
//! declaration, as the choice only excludes what it can tell apart.
bool Lowering::actualKind(const Expr& expr, VType::Kind& out) const {
    switch (expr.kind) {
    case Expr::INT_LIT:
    case Expr::PHYS_LIT: out = VType::INT; return true;
    case Expr::REAL_LIT: out = VType::REAL; return true;
    case Expr::TRUE_LIT:
    case Expr::FALSE_LIT: out = VType::LOGIC; return true;
    case Expr::BITS_LIT: out = VType::VECTOR; return true;
    case Expr::STRING_LIT: out = VType::ARRAY; return true;
    case Expr::QUAL: return markKind(expr.text, out);
    case Expr::PAREN: return expr.a && actualKind(*expr.a, out);
    case Expr::UNARY: {
        if (!visibleSubprograms(expr.text).empty()) return false;
        if (expr.text == "??") {
            out = VType::LOGIC;
            return true;
        }
        if (expr.text == "and" || expr.text == "or" || expr.text == "xor" || expr.text == "nand"
            || expr.text == "nor" || expr.text == "xnor") {
            VType operand;
            if (!actualType(*expr.a, operand)) return false;
            out = operand.isVector()
                      ? (operand.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC)
                      : operand.kind;
            return true;
        }
        return expr.a && actualKind(*expr.a, out);
    }
    case Expr::NAME:
    case Expr::SELECTED:
    case Expr::INDEX:
    case Expr::SLICE: {
        if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
            && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
            out = VType::LOGIC;
            return true;
        }
        VType type;
        if (typeOfDesignator(expr, type)) {
            out = type.kind;
            return true;
        }
        if (expr.kind != Expr::NAME) return false;
        // A constant the pre-scan folded has a value but no type entry of its own
        if (m_consts.count(expr.text) || m_preConsts.count(expr.text)) {
            out = VType::INT;
            return true;
        }
        if (m_realConsts.count(expr.text)) {
            out = VType::REAL;
            return true;
        }
        if (m_enumConsts.count(expr.text)) {
            out = VType::ENUM;
            return true;
        }
        return false;
    }
    case Expr::ATTR: {
        if (expr.text == "length" || expr.text == "pos" || expr.text == "to_integer") {
            out = VType::INT;
            return true;
        }
        // Signal attributes that yield a boolean
        if (expr.text == "event" || expr.text == "active" || expr.text == "quiet"
            || expr.text == "stable" || expr.text == "rising_edge"
            || expr.text == "falling_edge") {
            out = VType::LOGIC;
            return true;
        }
        if (expr.text == "high" || expr.text == "low" || expr.text == "left"
            || expr.text == "right") {
            VType prefix;
            bool known = false;
            if (expr.a && expr.a->kind == Expr::NAME) {
                TypeSpec spec;
                spec.flp = expr.flp;
                spec.mark = expr.a->text;
                known = formalType(spec, prefix);
            }
            if (!known && expr.a) known = typeOfDesignator(*expr.a, prefix);
            if (!known) return false;
            // An array attribute names the index type, which is a discrete scalar here
            out = prefix.isVector() ? VType::INT : prefix.kind;
            return true;
        }
        return false;
    }
    case Expr::BINARY:
        if (expr.text == "=" || expr.text == "/=" || expr.text == "<" || expr.text == ">"
            || expr.text == "<=" || expr.text == ">=" || expr.text == "?=" || expr.text == "?/="
            || expr.text == "?<" || expr.text == "?>" || expr.text == "?<="
            || expr.text == "?>=") {
            out = VType::LOGIC;  // The frontend models BOOLEAN as one logic bit
            return true;
        }
        if (!expr.a || !expr.b) return false;
        VType::Kind aKind;
        VType::Kind bKind;
        if (!actualKind(*expr.a, aKind) || !actualKind(*expr.b, bKind)) return false;
        if (aKind == bKind) {
            out = aKind;
            return true;
        }
        if (isVectorKind(aKind) && bKind == VType::INT) {
            out = aKind;  // A shift keeps its vector
            return true;
        }
        if (aKind == VType::INT && isVectorKind(bKind)) {
            out = bKind;
            return true;
        }
        if ((aKind == VType::INT || aKind == VType::REAL)
            && (bKind == VType::INT || bKind == VType::REAL)) {
            out = VType::REAL;  // Mixed-mode numeric arithmetic yields REAL
            return true;
        }
        return false;
    default: return false;
    }
}

//! Whether an actual of `actual` category may be associated with a formal of `formal`
//! category (IEEE 1076-2008 12.5). The frontend models no conversion except the numeric
//! literals' universal type, so the categories have to agree.
bool Lowering::kindAccepts(VType::Kind formal, VType::Kind actual) {
    if (formal == actual) return true;
    if (isVectorKind(formal) && isVectorKind(actual)) return true;
    if ((formal == VType::INT || formal == VType::REAL)
        && (actual == VType::INT || actual == VType::REAL)) {
        return true;
    }
    return false;
}

//! Types a declaration and a value present to overload resolution
bool Lowering::actualType(const Expr& expr, VType& out) const {
    switch (expr.kind) {
    case Expr::INT_LIT:
        out = VType{};
        out.kind = VType::INT;
        out.typeName = "integer";
        return true;
    case Expr::REAL_LIT:
        out = VType{};
        out.kind = VType::REAL;
        out.typeName = "real";
        return true;
    case Expr::TRUE_LIT:
    case Expr::FALSE_LIT:
        out = VType{};
        out.kind = VType::LOGIC;
        out.typeName = "boolean";
        return true;
    case Expr::PAREN: return expr.a && actualType(*expr.a, out);
    case Expr::UNARY: return actualTypeUnary(expr, out);
    case Expr::BINARY: return actualTypeBinary(expr, out);
    case Expr::QUAL: {
        TypeSpec spec;
        spec.flp = expr.flp;
        spec.mark = expr.text;
        return formalType(spec, out);
    }
    case Expr::NAME:
    case Expr::SELECTED:
    case Expr::SLICE: return actualTypeName(expr, out);
    case Expr::INDEX: return actualTypeIndex(expr, out);
    case Expr::ATTR: return actualTypeAttr(expr, out);
    case Expr::CALL: return actualTypeCall(expr, out);
    default: return false;
    }
}

//! Type a unary expression, or the element type a logical reduction yields
bool Lowering::actualTypeUnary(const Expr& expr, VType& out) const {
    if (!visibleSubprograms(expr.text).empty()) return false;
    // The explicit condition operator yields boolean (IEEE 1076-2008 9.2.9)
    if (expr.text == "??") {
        out = VType{};
        out.kind = VType::LOGIC;
        out.typeName = "boolean";
        return true;
    }
    if (expr.text == "and" || expr.text == "or" || expr.text == "xor" || expr.text == "nand"
        || expr.text == "nor" || expr.text == "xnor") {
        VType operand;
        if (!expr.a || !actualType(*expr.a, operand)) return false;
        if (operand.isVector()) {
            out = elementType(operand, expr.flp);
            out.typeName = operand.kwd == VBasicDTypeKwd::BIT ? "bit" : "std_ulogic";
        } else {
            out = operand;
        }
        return true;
    }
    return expr.a && actualType(*expr.a, out);
}

//! Result type of a binary operator over its operands' types
bool Lowering::actualTypeBinary(const Expr& expr, VType& out) const {
    const std::string& op = expr.text;
    if (op == "?=" || op == "?/=" || op == "?<" || op == "?<=" || op == "?>" || op == "?>=") {
        // A matching operator returns std_ulogic, not boolean (IEEE 1076-2008 9.2.3)
        out = VType{};
        out.kind = VType::LOGIC;
        out.typeName = "std_ulogic";
        return true;
    }
    if (op == "=" || op == "/=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        out = VType{};
        out.kind = VType::LOGIC;
        out.typeName = "boolean";
        return true;
    }
    VType aType;
    VType bType;
    const bool knownA = actualType(*expr.a, aType);
    const bool knownB = actualType(*expr.b, bType);
    // A user-declared operator decides the result type when its formals take both
    // operands; an operand with no type of its own leaves that choice open, so only
    // then does the operator stay unresolved
    if (!visibleSubprograms(op).empty()) {
        if (!knownA || !knownB) return false;
        const SubprogramDecl* matchp = nullptr;
        for (const SubprogramDecl* const subp : visibleSubprograms(op)) {
            if (subp->kind != SubprogramDecl::FUNCTION || !subp->hasBody || subp->impure) continue;
            const std::vector<std::pair<const FormalDecl*, std::string>> slots
                = formalSlots(*subp);
            if (slots.size() != 2) continue;
            VType formal0;
            VType formal1;
            if (!formalType(slots[0].first->type, formal0)
                || !formalType(slots[1].first->type, formal1)) {
                continue;
            }
            if (!typesMatch(formal0, aType) || !typesMatch(formal1, bType)) continue;
            if (matchp) return false;  // Ambiguous: the lowerer reports it
            matchp = subp;
        }
        if (matchp) return formalType(matchp->returnType, out);
    }
    if (!knownA || !knownB) return false;
    if (op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor" || op == "xnor") {
        if (aType.isVector() || bType.isVector()) {
            out = aType.isVector() ? aType : bType;
            out.width = std::max(aType.width, bType.width);
            if (aType.typeName != bType.typeName) out.typeName.clear();
        } else {
            out = aType;
            if (aType.typeName != bType.typeName) out.typeName.clear();
        }
        return true;
    }
    if (op == "&") {
        if (!aType.isVector() && !bType.isVector()) return false;
        out = aType.isVector() ? aType : bType;
        out.width = aType.width + bType.width;
        out.dtp = nullptr;
        if (aType.typeName != bType.typeName) out.typeName.clear();
        return true;
    }
    if (op == "sll" || op == "srl" || op == "sla" || op == "sra" || op == "rol" || op == "ror") {
        if (!aType.isVector() && !bType.isVector()) return false;
        out = aType.isVector() ? aType : bType;
        return true;
    }
    const auto physical = [](const VType& type) {
        return type.kind == VType::INT && !type.typeName.empty() && type.typeName != "integer";
    };
    // A fixed-point value with an integer or a real is a fixed-point value
    // (ieee.fixed_pkg); the bounds follow the operator and are fixed at lowering
    if (isFixedType(aType) && (bType.kind == VType::REAL || bType.isInt())) {
        out = aType;
        return true;
    }
    if (isFixedType(bType) && (aType.kind == VType::REAL || aType.isInt())) {
        out = bType;
        return true;
    }
    if (aType.kind == VType::INT && bType.kind == VType::INT) {
        // Addition and subtraction keep a physical type; a physical divided or
        // multiplied by an integer keeps it too, while physical/physical is an
        // integer (IEEE 1076-2008 9.2.5)
        const bool aPhysical = physical(aType);
        const bool bPhysical = physical(bType);
        if ((op == "+" || op == "-") && aType.typeName == bType.typeName) {
            out = aType;
        } else if ((op == "*" || op == "/") && aPhysical && bType.typeName == "integer") {
            out = aType;
        } else if ((op == "*" || op == "/") && bPhysical && aType.typeName == "integer") {
            out = bType;
        } else {
            out = intType(expr.flp);
        }
        return true;
    }
    if (aType.kind == VType::REAL || bType.kind == VType::REAL) {
        // A physical value multiplied or divided by a real keeps its type
        if ((op == "*" || op == "/") && physical(aType) && bType.kind == VType::REAL) {
            out = aType;
            return true;
        }
        if ((op == "*" || op == "/") && physical(bType) && aType.kind == VType::REAL) {
            out = bType;
            return true;
        }
        out = VType{};
        out.kind = VType::REAL;
        out.typeName = "real";
        return true;
    }
    if (aType.isVector() && bType.isVector()) {
        out = aType;
        out.width = op == "*" ? aType.width + bType.width : std::max(aType.width, bType.width);
        out.dtp = nullptr;
        if (aType.typeName != bType.typeName) out.typeName.clear();
        return true;
    }
    return false;
}

//! Type a name designator, including a bare call or a previously lowered constant
bool Lowering::actualTypeName(const Expr& expr, VType& out) const {
    if (typeOfDesignator(expr, out)) return true;
    if (expr.kind != Expr::NAME) return false;
    if (m_subprograms.count(expr.text)) {
        Expr call{Expr::CALL};
        call.flp = expr.flp;
        call.text = expr.text;
        call.spelling = expr.spelling;
        return actualType(call, out);
    }
    if (m_consts.count(expr.text) || m_preConsts.count(expr.text)) {
        out = VType{};
        out.kind = VType::INT;
        out.typeName = "integer";
        return true;
    }
    // A constant whose value was lowered before its type folded keeps its VHDL type
    // here, which is what an operator overload sees when it reads an operand
    const auto constTypeIt = m_constTypes.find(expr.text);
    if (constTypeIt != m_constTypes.end()) {
        out = constTypeIt->second;
        return true;
    }
    if (m_realConsts.count(expr.text)) {
        out = VType{};
        out.kind = VType::REAL;
        out.typeName = "real";
        return true;
    }
    return false;
}

//! Type an indexed expression, which may be an array index, a call or a conversion
bool Lowering::actualTypeIndex(const Expr& expr, VType& out) const {
    if (expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        out = VType{};
        out.kind = VType::LOGIC;
        out.typeName = "boolean";
        return true;
    }
    // A prefix that names a type mark is a conversion, not an index of a type object
    const bool typeMark = expr.a && expr.a->kind == Expr::NAME && m_types.count(expr.a->text) != 0;
    if (!typeMark && typeOfDesignator(expr, out)) return true;
    // The parser writes an array index, a function call, and a type conversion all as
    // INDEX; the prefix says which.
    if (!expr.a || expr.a->kind != Expr::NAME) return false;
    if (isToStringFunction(expr.a->text) && !m_subprograms.count(expr.a->text)) {
        out = stringType(expr.flp, 1);  // Its length is the lowered value's
        return true;
    }
    if (m_subprograms.count(expr.a->text)) {
        Expr call{Expr::CALL};
        call.flp = expr.flp;
        call.text = expr.a->text;
        call.spelling = expr.a->spelling;
        call.formalNames = expr.formalNames;
        for (const ExprP& argp : expr.args) {
            call.args.push_back(argp ? cloneExpr(*argp) : ExprP{});
        }
        return actualType(call, out);
    }
    // A type conversion carries its mark, and an unconstrained array mark takes the
    // operand's bounds (IEEE 1076-2008 9.3.6). Overload resolution needs this: without
    // it a user-defined operator over an unrelated type is selected for the built-in
    // operator, as poc's `"xor"(T_POLARITY, std_logic_vector)` was for
    // `std_logic_vector(z(...)) xor p`.
    if (expr.args.size() != 1 || !expr.args[0]) return false;
    VType operandType;
    if (!actualType(*expr.args[0], operandType)) return false;
    const auto declared = m_types.find(expr.a->text);
    if (declared != m_types.end()) {
        out = declared->second;
        if (!out.dtp) {
            out.width = operandType.width;
            out.left = operandType.left;
            out.right = operandType.right;
            out.ascending = operandType.ascending;
        }
        return true;
    }
    if (expr.a->text == "integer" || expr.a->text == "natural" || expr.a->text == "positive"
        || expr.a->text == "to_integer") {
        out = intType(expr.flp);
        return true;
    }
    if (!isConversion(expr.a->text) || !operandType.isVector()) return false;
    out = literalVectorType(expr.flp, expr.a->text, operandType.width);
    out.left = operandType.left;
    out.right = operandType.right;
    out.ascending = operandType.ascending;
    return true;
}

//! Type an attribute, from the attribute's own result or from its prefix type
bool Lowering::actualTypeAttr(const Expr& expr, VType& out) const {
    if (expr.text == "length" || expr.text == "pos" || expr.text == "to_integer") {
        out = intType(expr.flp);
        return true;
    }
    if (expr.text == "image") {
        out = stringType(expr.flp, 1);  // Its length is the lowered value's
        return true;
    }
    // Signal attributes that yield a boolean
    if (expr.text == "event" || expr.text == "active" || expr.text == "quiet"
        || expr.text == "stable" || expr.text == "rising_edge" || expr.text == "falling_edge") {
        out = VType{};
        out.kind = VType::LOGIC;
        out.typeName = "boolean";
        return true;
    }
    if (expr.text == "high" || expr.text == "low" || expr.text == "left" || expr.text == "right") {
        VType prefix;
        bool known = false;
        if (expr.a && expr.a->kind == Expr::NAME) {
            TypeSpec spec;
            spec.flp = expr.flp;
            spec.mark = expr.a->text;
            known = formalType(spec, prefix);
        }
        if (!known && expr.a) known = typeOfDesignator(*expr.a, prefix);
        if (!known) return false;
        out = prefix.isVector() ? intType(expr.flp) : prefix;
        return true;
    }
    return false;
}

//! Type a call by its conversion mark or by its single matching subprogram
bool Lowering::actualTypeCall(const Expr& expr, VType& out) const {
    TypeSpec spec;
    spec.flp = expr.flp;
    spec.mark = expr.text;
    if (formalType(spec, out)) return true;  // A type conversion carries its mark
    std::vector<const SubprogramDecl*> candidates;
    for (const SubprogramDecl* const subp : visibleSubprograms(expr.text)) {
        if (subp->kind != SubprogramDecl::FUNCTION || !subp->hasBody || subp->impure) continue;
        std::string why;
        if (!subprogramMatches(expr, *subp, why)) continue;
        if (!formalsAccept(expr, *subp)) continue;
        candidates.push_back(subp);
    }
    if (candidates.size() == 1) return formalType(candidates.front()->returnType, out);
    return false;
}

//! The base type of a formal, without needing a constraint. Constraints do not distinguish
//! overloads; the base element type and signing do.
bool Lowering::formalType(const TypeSpec& spec, VType& out) const {
    const std::string& mark = spec.mark;
    if (!spec.pkg.empty() && m_packagesp) {
        const auto pkgIt = m_packagesp->packages.find(spec.pkg);
        if (pkgIt != m_packagesp->packages.end()) {
            const auto typeIt = pkgIt->second.types.find(mark);
            if (typeIt != pkgIt->second.types.end()) {
                out = typeIt->second;
                return true;
            }
        }
    }
    if (m_ambiguousTypes.count(mark)) return false;
    const auto declared = m_types.find(mark);
    if (declared != m_types.end()) {
        out = declared->second;
        return true;
    }
    if (!spec.flp) return false;
    if (mark == "integer" || mark == "natural" || mark == "positive") {
        out = intType(spec.flp);
        return true;
    }
    if (mark == "real") {
        out = VType{};
        out.kind = VType::REAL;
        out.typeName = "real";
        return true;
    }
    if (mark == "boolean" || mark == "std_logic" || mark == "std_ulogic" || mark == "bit") {
        out = VType{};
        out.kind = mark == "bit" ? VType::BIT : VType::LOGIC;
        out.dtp = findBasic(spec.flp, mark == "bit" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC);
        out.typeName = mark == "bit" ? "bit" : (mark == "boolean" ? "boolean" : mark);
        return true;
    }
    // A character is a VHDL array element, not a numeric_std vector, so it signs like
    // `stringType` builds one; mismatched signing would make every string actual fail to
    // match a `string` formal
    if (mark == "character") {
        out = vectorType(spec.flp, VBasicDTypeKwd::LOGIC, VSigning::NOSIGN, 8);
        out.typeName = "character";
        return true;
    }
    if (mark == "string") {
        out.kind = VType::ARRAY;
        out.elemp = std::make_shared<VType>(
            vectorType(spec.flp, VBasicDTypeKwd::LOGIC, VSigning::NOSIGN, 8));
        out.elemp->typeName = "character";
        out.elemWidth = 8;
        out.kwd = VBasicDTypeKwd::LOGIC;
        out.signing = VSigning::NOSIGN;
        out.typeName = "string";
        return true;
    }
    if (mark == "time") {
        out = timeType(spec.flp);
        return true;
    }
    if (mark == "text" || mark == "line") {
        out = VType{};
        out.kind = mark == "line" ? VType::LINE : VType::FILE;
        out.typeName = mark;
        return true;
    }
    if (isUnconstrainedMark(mark) || mark == "u_unsigned" || mark == "u_signed") {
        out = VType{};
        out.kind = VType::VECTOR;
        out.kwd = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
        out.signing = vectorSigning(mark);
        out.typeName = (mark == "u_signed" || mark == "signed")
                           ? "signed"
                           : ((mark == "u_unsigned" || mark == "unsigned") ? "unsigned" : mark);
        return true;
    }
    return false;
}

//! Whether an actual may be associated with a formal of the given base type
bool Lowering::typesMatch(const VType& formal, const VType& actual) {
    // Universal integer literals also fit a real formal
    if (formal.kind == VType::REAL && actual.kind == VType::INT) return true;
    if (!formal.typeName.empty() && !actual.typeName.empty()) {
        if (!baseTypesRelated(formal.typeName, actual.typeName)) return false;
        // The same base type under different constraints, which is what an unconstrained
        // formal and a constrained actual are (IEEE 1076-2008 12.5)
        return true;
    }
    if (formal.kind != actual.kind && !(formal.isVector() && actual.isVector())) return false;
    if (formal.kind == VType::INT || formal.kind == VType::REAL) return true;
    if (formal.isVector() && actual.isVector()) {
        if (formal.kwd != actual.kwd) return false;
        if (formal.signing != actual.signing) return false;
        if (formal.elemWidth != actual.elemWidth) return false;
        if (formal.elemp && actual.elemp) return typesMatch(*formal.elemp, *actual.elemp);
    }
    return true;
}

//! Whether a numeric literal, possibly parenthesized or signed, is a universal value
bool Lowering::isNumericLiteral(const Expr& expr) {
    const Expr* ep = &expr;
    while (ep->kind == Expr::PAREN && ep->a) ep = ep->a.get();
    if (ep->kind == Expr::UNARY && ep->a && (ep->text == "-" || ep->text == "+")) {
        ep = ep->a.get();
        while (ep->kind == Expr::PAREN && ep->a) ep = ep->a.get();
    }
    return ep->kind == Expr::INT_LIT || ep->kind == Expr::REAL_LIT;
}

//! Whether two values of one base type have the same array or record shape. A null or
//! unconstrained width is left to the later lowering, which reports it if it matters.
bool Lowering::sameShape(const VType& target, const VType& value) {
    if (target.kind == VType::ARRAY && value.kind == VType::ARRAY) {
        if (target.width <= 0 || value.width <= 0) return true;
        if (target.length() != value.length()) return false;
        if (target.elemp && value.elemp) return sameShape(*target.elemp, *value.elemp);
        return true;
    }
    // A scalar's width is part of its type; the type mark already matched
    if (target.kind == VType::VECTOR && value.kind == VType::VECTOR) {
        if (target.width > 0 && value.width > 0) return target.width == value.width;
    } else if (target.kind == VType::RECORD && value.kind == VType::RECORD) {
        if (target.width > 0 && value.width > 0) return target.width == value.width;
    }
    return true;
}

//! A real literal with an integral value, possibly parenthesized or signed
bool integralRealLiteral(const Expr& expr, double& value) {
    const Expr* ep = &expr;
    while (ep->kind == Expr::PAREN && ep->a) ep = ep->a.get();
    if (ep->kind == Expr::UNARY && ep->a && (ep->text == "-" || ep->text == "+")) {
        ep = ep->a.get();
        while (ep->kind == Expr::PAREN && ep->a) ep = ep->a.get();
    }
    if (ep->kind != Expr::REAL_LIT) return false;
    value = ep->realValue;
    return std::floor(value) == value;
}

//! Whether a value may be assigned to a target of `target`'s base type. A value that has
//! no type of its own (an aggregate, a string literal) takes the target's type; a
//! character literal may only take a character-like one, and only a universal numeric
//! literal converts implicitly.
bool Lowering::valueConforms(const VType& target, const Expr& value, const char* what,
                             bool checkShape) const {
    if (value.kind == Expr::CHAR_LIT) {
        if (!charLiteralFits(target, value.charValue)) {
            value.flp->v3error(std::string{"VHDL: "} + what
                               + " character literal does not have the target's type.");
            return false;
        }
        return true;
    }
    if (target.typeName.empty()) return true;
    // A concatenation takes its result type from the target context, so only the
    // target category matters here (IEEE 1076-2008 9.2.5)
    if (value.kind == Expr::BINARY && value.text == "&") {
        if (!target.isVector()) {
            value.flp->v3error(std::string{"VHDL: "} + what + " does not have the target's type.");
            return false;
        }
        return true;
    }
    // A fixed-point result has the exact bounds its operator gave it (IEEE 1076-2008
    // fixed_pkg), unlike the width an operator result is otherwise guessed to have
    const Expr* resultp = &value;
    while (resultp->kind == Expr::PAREN && resultp->a) resultp = resultp->a.get();
    const auto lowered = m_exprTypes.find(resultp);
    if (checkShape && lowered != m_exprTypes.end() && isFixedType(lowered->second.type)
        && isFixedType(target) && lowered->second.type.width != target.width) {
        value.flp->v3error(std::string{"VHDL: "} + what + " of type '"
                           + lowered->second.type.typeName
                           + "' does not have the target's length.");
        return false;
    }
    VType source;
    if (actualType(value, source) && !source.typeName.empty()) {
        // A universal integer literal takes any integer type, but not a physical type
        // (IEEE 1076-2008 9.3.1)
        if (source.kind == VType::INT && target.kind == VType::INT && target.typeName == "integer"
            && isNumericLiteral(value)) {
            return true;
        }
        if (source.kind == VType::INT && target.kind == VType::REAL && isNumericLiteral(value)) {
            return true;
        }
        // A universal real literal with an integral value takes an integer type, but
        // not a physical type
        if (source.kind == VType::REAL && target.kind == VType::INT
            && target.typeName == "integer") {
            double integral = 0.0;
            if (integralRealLiteral(value, integral)) return true;
        }
        // std_logic is the resolved subtype of std_ulogic (IEEE 1164)
        if (!baseTypesRelated(source.typeName, target.typeName)) {
            value.flp->v3error(std::string{"VHDL: "} + what + " of type '" + source.typeName
                               + "' does not have the target's base type '" + target.typeName
                               + "'.");
            return false;
        }
        // Only a simple designator has a width this pass can trust: a call or a
        // conversion is constrained by its context, and an operator result carries no
        // declared bounds.
        const bool sized = checkShape
                           && (value.kind == Expr::NAME || value.kind == Expr::SELECTED
                               || value.kind == Expr::SLICE);
        if (sized && !sameShape(target, source)) {
            value.flp->v3error(std::string{"VHDL: "} + what + " of type '" + source.typeName
                               + "' does not have the target's length.");
            return false;
        }
        return true;
    }
    VType::Kind sourceKind;
    if (!actualKind(value, sourceKind)) return true;
    // A bare enumeration literal is typed by the target. Reject one that the target
    // type does not declare; a shared literal name stays a valid choice (IEEE 1076-2008
    // 12.5)
    if (target.kind == VType::ENUM && value.kind == Expr::NAME
        && std::find(target.literals.begin(), target.literals.end(), value.text)
               == target.literals.end()) {
        value.flp->v3error(std::string{"VHDL: "} + what
                           + " uses an enumeration literal of another type.");
        return false;
    }
    if (!kindAccepts(target.kind, sourceKind)) {
        value.flp->v3error(std::string{"VHDL: "} + what + " does not have the target's type.");
        return false;
    }
    return true;
}

//! Whether a type conversion relates its target and operand. Arrays convert to arrays with
//! the same element type, numeric types to numeric types, and a subtype to its base; a
//! conversion between unrelated types is an error (IEEE 1076-2008 9.3.6).
bool Lowering::conversionRelated(const VType& target, const VType& source) {
    const bool targetNumeric = target.kind == VType::INT || target.kind == VType::REAL;
    const bool sourceNumeric = source.kind == VType::INT || source.kind == VType::REAL;
    if (targetNumeric && sourceNumeric) {
        // The predefined numeric types convert among themselves; a physical type only
        // converts to itself (IEEE 1076-2008 9.3.6)
        const auto builtinNumeric = [](const VType& type) {
            return type.typeName == "integer" || type.typeName == "real";
        };
        if (builtinNumeric(target) && builtinNumeric(source)) return true;
        return !target.typeName.empty() && target.typeName == source.typeName;
    }
    // An enumeration converts to and from its position number
    if ((target.kind == VType::ENUM && source.kind == VType::INT)
        || (target.kind == VType::INT && source.kind == VType::ENUM)) {
        return true;
    }
    if (target.typeName == "character" && source.kind == VType::INT) return true;
    if (target.isVector() && source.isVector()) return true;
    if (!target.typeName.empty() && !source.typeName.empty()) {
        if (baseTypesRelated(target.typeName, source.typeName)) return true;
    }
    // A literal or aggregate has no type of its own; the mark types it
    return source.typeName.empty();
}

//! Whether two operands of a logical operator have the same element type. VHDL allows
//! an array and a scalar of its element type, and a character literal is an element of
//! either a logic or a bit array (IEEE 1076-2008 9.2.2).
bool Lowering::logicalOperandsConform(const Expr& a, const Expr& b, const VType& aLowered,
                                      const VType& bLowered) const {
    // A comparison expression lowers to a logic bit but is a boolean; prefer the VHDL
    // type where the expression has one, and fall back to the lowered dtype otherwise
    VType aTyped;
    VType bTyped;
    const VType& aType = actualType(a, aTyped) ? aTyped : aLowered;
    const VType& bType = actualType(b, bTyped) ? bTyped : bLowered;
    const auto nonLogical = [](const VType& type) {
        return type.kind == VType::INT || type.kind == VType::REAL || type.kind == VType::ENUM
               || type.kind == VType::FILE || type.kind == VType::LINE;
    };
    if (nonLogical(aType) || nonLogical(bType)) {
        a.flp->v3error("VHDL: logical operator operand is not a logic or bit value.");
        return false;
    }
    // Two arrays must be of the same VHDL array type, not merely of the same element
    // kind: unsigned and std_logic_vector are unrelated (IEEE 1076-2008 9.2.2)
    if (aType.isVector() && bType.isVector() && !aType.typeName.empty() && !bType.typeName.empty()
        && !arrayBaseTypesRelated(aType.typeName, bType.typeName)) {
        a.flp->v3error("VHDL: logical operator operands do not have the same base type.");
        return false;
    }
    // boolean and std_logic are distinct enumeration types; a character literal is an
    // element of the logic or bit type beside it, never a boolean
    if ((a.kind == Expr::CHAR_LIT && bType.typeName == "boolean")
        || (b.kind == Expr::CHAR_LIT && aType.typeName == "boolean")) {
        a.flp->v3error("VHDL: logical operator operands do not have the same element type.");
        return false;
    }
    const auto elementName = [](const VType& type) -> std::string {
        if (type.kind == VType::ARRAY && type.elemp) return type.elemp->typeName;
        if (type.kind == VType::VECTOR) {
            return type.kwd == VBasicDTypeKwd::BIT ? "bit" : "std_ulogic";
        }
        return type.typeName;
    };
    // A character literal is a logic or bit element, whichever the other operand needs
    const std::string aName = a.kind == Expr::CHAR_LIT ? "" : elementName(aType);
    const std::string bName = b.kind == Expr::CHAR_LIT ? "" : elementName(bType);
    if (!aName.empty() && !bName.empty() && !baseTypesRelated(aName, bName)) {
        a.flp->v3error("VHDL: logical operator operands do not have the same element type.");
        return false;
    }
    return true;
}

//! Whether two operands of a relational operator have related base types. A numeric
//! vector compares with an integer only where a package defines that operator; a scalar
//! logic value and a number are unrelated (IEEE 1076-2008 9.2.3, 12.5).
bool Lowering::relationalOperandsConform(const Expr& a, const Expr& b) const {
    VType aType;
    VType bType;
    const bool aKnown = actualType(a, aType);
    const bool bKnown = actualType(b, bType);
    if (a.kind == Expr::CHAR_LIT || b.kind == Expr::CHAR_LIT) {
        // A character literal compares with a logic/bit scalar, a character, or an
        // enumeration type that declares it (IEEE 1076-2008 9.2.3, 12.5)
        if (a.kind == Expr::CHAR_LIT && b.kind == Expr::CHAR_LIT) return true;
        const Expr& literal = a.kind == Expr::CHAR_LIT ? a : b;
        const Expr& other = a.kind == Expr::CHAR_LIT ? b : a;
        const bool otherKnown = a.kind == Expr::CHAR_LIT ? bKnown : aKnown;
        const VType& otherType = a.kind == Expr::CHAR_LIT ? bType : aType;
        // An element of a string loses the character type name in the packed model; its
        // declared array still says every element is a character
        bool characterElement = false;
        if ((other.kind == Expr::INDEX || other.kind == Expr::SLICE) && other.a) {
            VType prefixType;
            if (typeOfDesignator(*other.a, prefixType)) {
                characterElement = prefixType.kind == VType::ARRAY && prefixType.elemWidth == 8
                                   && (!prefixType.elemp || prefixType.elemp->typeName.empty()
                                       || prefixType.elemp->typeName == "character");
            }
        }
        if (otherKnown && !characterElement && !charLiteralFits(otherType, literal.charValue)) {
            literal.flp->v3error("VHDL: relational operator compares a character with "
                                 "an unrelated type.");
            return false;
        }
        return true;
    }
    if (!aKnown || !bKnown) return true;
    const bool aNumeric = aType.kind == VType::INT || aType.kind == VType::REAL;
    const bool bNumeric = bType.kind == VType::INT || bType.kind == VType::REAL;
    if (aNumeric && bNumeric) {
        if (aType.kind != bType.kind) {
            a.flp->v3error("VHDL: relational operator compares a real with an integer.");
            return false;
        }
        if (aType.kind == VType::INT && !aType.typeName.empty() && !bType.typeName.empty()
            && aType.typeName != bType.typeName) {
            a.flp->v3error("VHDL: relational operator operands do not have the same base type.");
            return false;
        }
        return true;
    }
    if (aNumeric || bNumeric) {
        const VType& other = aNumeric ? bType : aType;
        if (other.isVector()) {
            const bool numericVector = other.typeName == "unsigned" || other.typeName == "signed"
                                       || other.typeName == "u_unsigned"
                                       || other.typeName == "u_signed";
            if (numericVector) return true;
            const bool logicVector
                = other.typeName == "std_logic_vector" || other.typeName == "std_ulogic_vector";
            if (logicVector
                && (m_stdLogicArith || m_stdLogicUnsigned || m_stdLogicSigned
                    || m_numericStdUnsigned || m_numericStdSigned)) {
                return true;
            }
            if (other.typeName.empty()) return true;
        }
        a.flp->v3error("VHDL: relational operator operands do not have the same base type.");
        return false;
    }
    if (aType.isVector() && bType.isVector()) {
        // std_logic_arith declares comparisons between its signed and unsigned
        // vectors, and between either and an integer; numeric_std does not
        const auto numericVector = [](const std::string& name) {
            return name == "unsigned" || name == "signed" || name == "u_unsigned"
                   || name == "u_signed";
        };
        if (numericVector(aType.typeName) && numericVector(bType.typeName)
            && (m_stdLogicArith || m_stdLogicUnsigned || m_stdLogicSigned)) {
            return true;
        }
        // Otherwise both operands must be of one VHDL array type (or closely related
        // logic subtypes); std_logic_vector and unsigned are unrelated (IEEE 1076-2008
        // 9.2.3)
        if (!aType.typeName.empty() && !bType.typeName.empty()
            && !baseTypesRelated(aType.typeName, bType.typeName)) {
            a.flp->v3error("VHDL: relational operator operands do not have the same base type.");
            return false;
        }
        return true;
    }
    if (aType.isVector() || bType.isVector()) {
        a.flp->v3error("VHDL: relational operator compares an array with a scalar.");
        return false;
    }
    if (!aType.typeName.empty() && !bType.typeName.empty()
        && !baseTypesRelated(aType.typeName, bType.typeName)) {
        a.flp->v3error("VHDL: relational operator operands do not have the same base type.");
        return false;
    }
    return true;
}

//! Whether every actual of a call can be associated with the formal it fills. A caller asks
//! only to choose between declarations that already accept the actuals' count and names.
bool Lowering::formalsAccept(const Expr& call, const SubprogramDecl& sub) const {
    const std::vector<std::pair<const FormalDecl*, std::string>> slots = formalSlots(sub);
    std::size_t positional = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const Expr* const actualp = call.args[i].get();
        if (!actualp) return true;
        const std::string formal = i < call.formalNames.size() ? call.formalNames[i] : "";
        const FormalDecl* formalp = nullptr;
        if (formal.empty()) {
            if (positional >= slots.size()) return true;
            formalp = slots[positional++].first;
        } else {
            for (const auto& slot : slots) {
                if (slot.second == formal) formalp = slot.first;
            }
            if (!formalp) return true;
        }
        VType aType;
        VType fType;
        if (actualType(*actualp, aType) && formalType(formalp->type, fType)) {
            if (!typesMatch(fType, aType)) {
                // A universal integer literal takes any integer subtype
                // (IEEE 1076-2008 9.3.1)
                if (fType.kind == VType::INT && fType.typeName == "integer"
                    && aType.kind == VType::INT && isNumericLiteral(*actualp)) {
                    continue;
                }
                return false;
            }
            continue;
        }
        VType::Kind aKind;
        VType::Kind fKind;
        if (!actualKind(*actualp, aKind) || !markKind(formalp->type.mark, fKind)) continue;
        if (!kindAccepts(fKind, aKind)) return false;
    }
    return true;
}

//! Whether every known actual has exactly the base type of its formal, which picks one
//! declaration when the category-level test left several.
bool Lowering::formalsExact(const Expr& call, const SubprogramDecl& sub) const {
    const std::vector<std::pair<const FormalDecl*, std::string>> slots = formalSlots(sub);
    std::size_t positional = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const Expr* const actualp = call.args[i].get();
        if (!actualp) return false;
        const std::string formal = i < call.formalNames.size() ? call.formalNames[i] : "";
        const FormalDecl* formalp = nullptr;
        if (formal.empty()) {
            if (positional >= slots.size()) return false;
            formalp = slots[positional++].first;
        } else {
            for (const auto& slot : slots) {
                if (slot.second == formal) formalp = slot.first;
            }
            if (!formalp) return false;
        }
        VType aType;
        VType fType;
        if (!actualType(*actualp, aType) || !formalType(formalp->type, fType)) return false;
        // An integer actual may be associated with a real formal, but does not match it
        // exactly: a universal integer literal or an integer object takes the integer
        // overload when both are visible (IEEE 1076-2008 12.5)
        if (fType.kind == VType::REAL && aType.kind == VType::INT) return false;
        if (!typesMatch(fType, aType)) return false;
    }
    return true;
}

//! A user-defined infix operator whose formals exactly match the operands
AstNodeExpr* Lowering::lowerUserOperator(const Expr& expr, const VType* contextp, bool& found) {
    found = false;
    if (!expr.a || !expr.b) return nullptr;
    VType actual0;
    VType actual1;
    const bool have0 = actualType(*expr.a, actual0);
    const bool have1 = actualType(*expr.b, actual1);
    // A bit-string literal, aggregate, or nested expression has no type of its own.
    // The other operand, the expected result type, or a bit-string's width can fix
    // the operator; with none of those the built-in operator applies.
    const auto literalWidth = [](const Expr& operand) {
        return operand.kind == Expr::BITS_LIT ? static_cast<int>(operand.bits.size()) : 0;
    };
    const int width0 = literalWidth(*expr.a);
    const int width1 = literalWidth(*expr.b);
    // Two untyped operands are resolvable only when both are sized literals; a
    // pair of conversions or calls keeps its built-in operator.
    if (!have0 && !have1 && (width0 == 0 || width1 == 0)) return nullptr;
    std::vector<const SubprogramDecl*> matches;
    for (const SubprogramDecl* const subp : visibleSubprograms(expr.text)) {
        if (subp->kind != SubprogramDecl::FUNCTION || !subp->hasBody || subp->impure) continue;
        const std::vector<std::pair<const FormalDecl*, std::string>> slots = formalSlots(*subp);
        if (slots.size() != 2) continue;
        VType formal0;
        VType formal1;
        if (!formalType(slots[0].first->type, formal0)
            || !formalType(slots[1].first->type, formal1)) {
            continue;
        }
        if (have0 && !typesMatch(formal0, actual0)) continue;
        if (have1 && !typesMatch(formal1, actual1)) continue;
        // A formal's base type has no constraint width, so compare a bit-string
        // literal against the width its full type spec gives.
        if (!have0 && width0 > 0) {
            VType exact;
            if (resolveType(slots[0].first->type, exact) && exact.width > 0
                && exact.width != width0) {
                continue;
            }
        }
        if (!have1 && width1 > 0) {
            VType exact;
            if (resolveType(slots[1].first->type, exact) && exact.width > 0
                && exact.width != width1) {
                continue;
            }
        }
        if (contextp) {
            VType returnType;
            if (formalType(subp->returnType, returnType) && !typesMatch(returnType, *contextp)) {
                continue;
            }
        }
        matches.push_back(subp);
    }
    if (matches.empty()) return nullptr;
    found = true;
    if (matches.size() > 1) {
        expr.flp->v3error("VHDL: operator '" + expr.text + "' is ambiguous for these operands.");
        return nullptr;
    }
    Expr call{Expr::CALL};
    call.flp = expr.flp;
    call.text = expr.text;
    call.spelling = expr.spelling;
    call.args.push_back(cloneExpr(*expr.a));
    call.args.push_back(cloneExpr(*expr.b));
    return lowerFunctionCall(call, contextp, matches.front());
}

//! A user-defined prefix operator whose formal exactly matches the operand
AstNodeExpr* Lowering::lowerUserUnary(const Expr& expr, const VType* contextp, bool& found) {
    found = false;
    if (!expr.a) return nullptr;
    VType actual;
    if (!actualType(*expr.a, actual)) return nullptr;
    std::vector<const SubprogramDecl*> matches;
    for (const SubprogramDecl* const subp : visibleSubprograms(expr.text)) {
        if (subp->kind != SubprogramDecl::FUNCTION || !subp->hasBody || subp->impure) continue;
        const std::vector<std::pair<const FormalDecl*, std::string>> slots = formalSlots(*subp);
        if (slots.size() != 1) continue;
        VType formal;
        if (!formalType(slots[0].first->type, formal)) continue;
        if (!typesMatch(formal, actual)) continue;
        if (contextp) {
            VType returnType;
            if (formalType(subp->returnType, returnType) && !typesMatch(returnType, *contextp)) {
                continue;
            }
        }
        matches.push_back(subp);
    }
    if (matches.empty()) return nullptr;
    found = true;
    if (matches.size() > 1) {
        expr.flp->v3error("VHDL: operator '" + expr.text + "' is ambiguous for this operand.");
        return nullptr;
    }
    Expr call{Expr::CALL};
    call.flp = expr.flp;
    call.text = expr.text;
    call.spelling = expr.spelling;
    call.args.push_back(cloneExpr(*expr.a));
    return lowerFunctionCall(call, contextp, matches.front());
}

void Lowering::captureBaseFor(const SubprogramRef& ref, ScopeSave& base, InlineEnv& env) const {
    // The scope a call captures: an active inlining's environment, else the process that
    // declared it, else the module's declarative part
    if (ref.frame >= 0) {
        base = m_inlineFrames[ref.frame].liveScope;
        if (m_inlineFrames[ref.frame].envp) env = cloneInlineEnv(*m_inlineFrames[ref.frame].envp);
        return;
    }
    if (ref.processLocal) {
        base = m_inlineFrames.empty() ? saveScope() : m_inlineFrames.front().entryScope;
        return;
    }
    base = m_moduleScope;
}

Lowering::InlineEnv Lowering::cloneInlineEnv(const InlineEnv& env) {
    InlineEnv out;
    for (const auto& entry : env) {
        out[entry.first] = entry.second;
        out[entry.first].exprp = entry.second.exprp->cloneTree(false);
    }
    return out;
}

void Lowering::deleteInlineEnv(InlineEnv& env) {
    for (auto& entry : env) entry.second.exprp->deleteTree();
    env.clear();
}

AstNodeExpr* Lowering::inlineBinding(FileLine* flp, const std::string& name, bool write) const {
    const InlineEnv& bindings = activeEnv();
    const auto it = bindings.find(name);
    if (it == bindings.end()) return nullptr;
    const InlineBinding& binding = it->second;
    if (write && !binding.lvalue) {
        flp->v3error("VHDL: cannot assign to formal argument '" + name + "'.");
        return nullptr;
    }
    if (!write && !binding.readable) {
        flp->v3error("VHDL: out mode parameter '" + name
                     + "' may not be read before the procedure returns.");
        return nullptr;
    }
    AstNodeExpr* const clonep = binding.exprp->cloneTree(false);
    if (binding.lvalue) {
        clonep->foreach([write](AstVarRef* const refp) {
            refp->access(write ? VAccess::WRITE : VAccess::READ);
        });
    }
    return clonep;
}

bool Lowering::bindSubprogramFormals(const Expr& call, const SubprogramDecl& sub, InlineEnv& env,
                                     AstNodeStmt*& prologsp, bool lvalueFormals, bool concurrent) {
    std::vector<std::pair<const FormalDecl*, std::string>> slots;
    for (const FormalDecl& formal : sub.formals)
        for (const auto& name : formal.names) slots.emplace_back(&formal, name.first);
    std::size_t positional = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i)
        if (i >= call.formalNames.size() || call.formalNames[i].empty()) ++positional;
    if (positional > slots.size()) {
        unsupportedError(call.flp, "more actuals than formals in call to '" + sub.name + "'");
        return false;
    }
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const bool named = i < call.formalNames.size() && !call.formalNames[i].empty();
        if (named && i < positional) {
            unsupportedError(call.flp, "positional actual after a named one");
            return false;
        }
        const std::string formalName = named ? call.formalNames[i] : slots[i].second;
        const FormalDecl* formalp = nullptr;
        for (const auto& slot : slots)
            if (slot.second == formalName) {
                formalp = slot.first;
                break;
            }
        if (!formalp) {
            unsupportedError(call.flp,
                             "no formal named '" + formalName + "' in '" + sub.name + "'");
            return false;
        }
        if (!lvalueFormals && formalp->mode != PortMode::IN) {
            unsupportedError(formalp->flp, "function formal not in mode in");
            return false;
        }
        // Out and inout parameters, and variables, bind the actual's location itself
        const bool asLvalue
            = lvalueFormals
              && (formalp->mode != PortMode::IN || formalp->cls == FormalDecl::VARIABLE);
        const Expr& actual = *call.args[i];
        VType formalType;
        const VType* ctxp = nullptr;
        const std::string& formalMark = formalp->type.mark;
        const bool predefinedUnconstrained
            = !formalp->type.hasRange && formalp->type.constraintMark.empty()
              && formalp->type.indexConstraints.empty()
              && (formalMark == "std_logic_vector" || formalMark == "std_ulogic_vector"
                  || formalMark == "bit_vector" || formalMark == "unsigned"
                  || formalMark == "signed" || formalMark == "u_unsigned"
                  || formalMark == "u_signed");
        bool haveFormalType = false;
        if (!predefinedUnconstrained && resolveType(formalp->type, formalType)) {
            if (formalType.isVector() && formalType.width <= 0) {
                // An unconstrained array formal takes its bounds from the actual: an
                // aggregate names its own range (IEEE 1076-2008 9.3.3.3), while a name
                // brings its own through typeOfDesignator below
                constrainAggregateType(actual, formalType);
                haveFormalType = formalType.width > 0;
            } else {
                haveFormalType = formalType.isVector() || formalType.dtp != nullptr;
            }
        }
        if (haveFormalType) ctxp = &formalType;
        AstNodeExpr* actualp = asLvalue ? lowerDesignator(actual, true) : lowerExpr(actual, ctxp);
        if (!actualp) return false;
        InlineBinding binding{actualp, asLvalue, true, VType{}, nullptr};
        // The actual's VHDL type first: an array's element width and index bounds are gone
        // from its data type, and a formal indexing the actual needs both
        const bool haveActualType = typeOfDesignator(actual, binding.type);
        // A string formal's element is a character whatever the actual is; a null actual
        // has no element to take that width from, and a packed fallback type would give
        // the formal one-bit elements
        if (formalMark == "string"
            && !(haveActualType && binding.type.kind == VType::ARRAY
                 && binding.type.elemWidth == 8)) {
            binding.type = stringType(formalp->flp, actualp->width() / 8);
        } else if (!haveActualType) {
            if (ctxp) {
                binding.type = *ctxp;
            } else {
                binding.type = typeOfDType(actualp->dtypep());
                // The actual is an aggregate or another untyped value: its base type is
                // the formal's, and only its bounds come from the value
                VType formalBase;
                if (this->formalType(formalp->type, formalBase) && !formalBase.typeName.empty()) {
                    binding.type.typeName = formalBase.typeName;
                    binding.type.signing = formalBase.signing;
                    binding.type.kwd = formalBase.kwd;
                }
            }
        }
        // A scalar formal keeps its declared subtype: case coverage and attributes see
        // the formal's bounds, not the actual's narrower ones.
        if (haveFormalType && !formalType.isVector()) binding.type = formalType;
        // A constrained array formal indexes by its own bounds, whatever the actual's are
        // (IEEE 1076-2008 4.2.2.1); an unconstrained one takes the actual's
        if (haveFormalType && formalType.isVector() && binding.type.isVector()
            && !formalType.unconstrainedBase && formalType.length() == binding.type.length()) {
            binding.type.left = formalType.left;
            binding.type.right = formalType.right;
            binding.type.ascending = formalType.ascending;
        }
        if (asLvalue && formalp->mode == PortMode::OUT && formalp->cls == FormalDecl::SIGNAL) {
            // Reading an out signal formal before return would see the actual's old value
            binding.readable = false;
        }
        // An out parameter starts at its default value; a concurrent call's body drives
        // the actual with continuous assignments, so a prologue would add a second driver
        if (asLvalue && formalp->mode == PortMode::OUT && !concurrent) {
            FileLine* const pflp = formalp->flp;
            AstNodeExpr* const defp = resizeExpr(makeIntConst(pflp, 0), binding.type, pflp);
            AstNodeStmt* const initp
                = formalp->cls == FormalDecl::SIGNAL
                      ? static_cast<AstNodeStmt*>(
                            new AstAssignDly{pflp, actualp->cloneTree(false), defp})
                      : static_cast<AstNodeStmt*>(
                            new AstAssign{pflp, actualp->cloneTree(false), defp});
            prologsp
                = prologsp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(prologsp, initp) : initp;
        }
        env[formalName] = std::move(binding);
    }
    // Formals without an actual take the default value of their declaration
    for (std::size_t s = 0; s < slots.size(); ++s) {
        if (s < positional || env.count(slots[s].second)) continue;
        const FormalDecl& formal = *slots[s].first;
        if (!formal.init) {
            unsupportedError(call.flp, "missing actual for formal '" + slots[s].second
                                           + "' in call to '" + sub.name + "'");
            return false;
        }
        AstNodeExpr* const initp = lowerExpr(*formal.init, nullptr);
        if (!initp) return false;
        InlineBinding binding{initp, false, true, VType{}, nullptr};
        binding.type = typeOfDType(initp->dtypep());
        env[slots[s].second] = std::move(binding);
    }
    return true;
}

//! `read`/`hread` take one value of `width` bits from the front of a line and leave
//! the rest of it unread, which is what GHDL and nvc both do
bool fileReadValue(const std::string& line, size_t& pos, int width, bool hex, std::string& bits) {
    const size_t want = hex ? static_cast<size_t>((width + 3) / 4) : static_cast<size_t>(width);
    std::string found;
    size_t i = pos;
    while (found.size() < want && i < line.size()) {
        const char c = line[i];
        const bool ok = hex ? std::isxdigit(static_cast<unsigned char>(c)) != 0
                            : std::strchr("01xXzZwWlLhH-", c) != nullptr;
        if (!ok) break;
        found += c;
        ++i;
    }
    if (found.empty()) return false;
    pos = i;
    bits.clear();
    for (const char c : found) {
        if (!hex) {
            bits += (c == '1' || c == 'H' || c == 'h') ? '1' : '0';
            continue;
        }
        const int v = std::isdigit(static_cast<unsigned char>(c))
                          ? c - '0'
                          : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
        for (int b = 3; b >= 0; --b) bits += char('0' + ((v >> b) & 1));
    }
    // The value is right-aligned in the target, so keep its low bits
    if (static_cast<int>(bits.size()) > width) {
        bits.erase(0, bits.size() - static_cast<size_t>(width));
    }
    while (static_cast<int>(bits.size()) < width) bits.insert(bits.begin(), '0');
    return true;
}

bool Lowering::evalFileExpr(const Expr& expr, FileVal& out, FileVals& vals, FileReaders& files) {
    out = FileVal{};
    switch (expr.kind) {
    case Expr::INT_LIT: out.i = expr.intValue; return true;
    case Expr::TRUE_LIT: out.i = 1; return true;
    case Expr::FALSE_LIT: out.i = 0; return true;
    case Expr::BITS_LIT:
        out.bits = expr.bits;
        out.width = static_cast<int>(expr.bits.size());
        return true;
    case Expr::STRING_LIT: out.text = expr.spelling; return true;
    case Expr::NAME: {
        const auto it = vals.find(expr.text);
        if (it != vals.end()) {
            out = it->second;
            return true;
        }
        const auto textIt = m_stringConsts.find(expr.text);
        if (textIt != m_stringConsts.end()) {
            out.text = textIt->second;
            return true;
        }
        return evalInt(expr, out.i);
    }
    case Expr::PAREN: return evalFileExpr(*expr.a, out, vals, files);
    case Expr::UNARY: {
        FileVal operand;
        if (!evalFileExpr(*expr.a, operand, vals, files)) return false;
        out.i = expr.text == "-" ? -operand.i : operand.i;
        return true;
    }
    case Expr::BINARY: {
        FileVal lhs;
        FileVal rhs;
        if (!evalFileExpr(*expr.a, lhs, vals, files)) return false;
        if (!evalFileExpr(*expr.b, rhs, vals, files)) return false;
        const std::string& op = expr.text;
        if (op == "+") {
            out.i = lhs.i + rhs.i;
        } else if (op == "-") {
            out.i = lhs.i - rhs.i;
        } else if (op == "*") {
            out.i = lhs.i * rhs.i;
        } else if (op == "/" && rhs.i) {
            out.i = lhs.i / rhs.i;
        } else if (op == "mod" && rhs.i) {
            out.i = lhs.i % rhs.i;
        } else {
            return false;
        }
        return true;
    }
    case Expr::INDEX:
        // `endfile(f)`: whether the file has no line left to read
        if (expr.a && expr.a->kind == Expr::NAME && expr.a->text == "endfile"
            && expr.args.size() == 1 && expr.args[0]->kind == Expr::NAME) {
            const auto it = files.find(expr.args[0]->text);
            out.i = it == files.end() || it->second.eof ? 1 : 0;
            return true;
        }
        return false;
    default: return false;
    }
}

bool Lowering::fileAssign(const Stmt& stmt, FileVal& value, FileVals& vals, FileReaders& files) {
    const Expr& target = *stmt.target;
    if (target.kind == Expr::NAME) {
        FileVal& slot = vals[target.text];
        if (!value.bits.empty()) {
            if (!slot.width) slot.width = value.width;
            slot.bits = value.bits;
            return true;
        }
        slot = value;
        return true;
    }
    // An element of an image: it is kept by index, and placed in the packed value
    // once the bounds of the array it belongs to are known
    if (target.kind == Expr::INDEX && target.a && target.a->kind == Expr::NAME
        && target.args.size() == 1 && !value.bits.empty()) {
        FileVal index;
        if (!evalFileExpr(*target.args[0], index, vals, files)) return false;
        FileVal& slot = vals[target.a->text];
        slot.width = static_cast<int>(value.bits.size());
        slot.elems[static_cast<size_t>(index.i)] = value.bits;
        return true;
    }
    unsupportedError(stmt.flp, "assignment target in a subprogram that reads a file");
    return false;
}

bool Lowering::fileCall(const Stmt& stmt, FileVals& vals, FileReaders& files) {
    const Expr& call = *stmt.value;
    // A predefined file operation is written like a call, and parses like an index
    std::string name;
    if (call.kind == Expr::CALL) {
        name = call.text;
    } else if (call.kind == Expr::INDEX && call.a && call.a->kind == Expr::NAME) {
        name = call.a->text;
    }
    if (call.args.size() != 2 || call.args[0]->kind != Expr::NAME
        || call.args[1]->kind != Expr::NAME) {
        unsupportedError(stmt.flp, "file operation with unexpected arguments");
        return false;
    }
    const std::string& file = call.args[0]->text;
    const std::string& var = call.args[1]->text;
    if (name == "readline") {
        const auto it = files.find(file);
        if (it == files.end() || !it->second.in.is_open()) {
            unsupportedError(stmt.flp, "read of a file that is not open");
            return false;
        }
        std::string line;
        if (!std::getline(it->second.in, line)) {
            it->second.eof = true;
            return false;
        }
        it->second.eof = false;
        // A line variable holds the text, and its cursor in i
        FileVal& slot = vals[var];
        slot.text = line;
        slot.i = 0;
        slot.bits.clear();
        return true;
    }
    if (name != "read" && name != "hread") {
        unsupportedError(stmt.flp, "file operation '" + name + "'");
        return false;
    }
    const auto lineIt = vals.find(file);
    if (lineIt == vals.end()) {
        unsupportedError(stmt.flp, "read from a line that was never filled");
        return false;
    }
    // The width is the target's, not the line's: the line holds text only
    const int width = vals[call.args[1]->text].width;
    if (width <= 0) {
        unsupportedError(stmt.flp, "read into a value of unknown width");
        return false;
    }
    std::string bits;
    size_t pos = static_cast<size_t>(lineIt->second.i);
    if (!fileReadValue(lineIt->second.text, pos, width, name == "hread", bits)) {
        unsupportedError(stmt.flp, "read past the end of a line");
        return false;
    }
    lineIt->second.i = static_cast<int64_t>(pos);
    FileVal& slot = vals[call.args[1]->text];
    slot.bits = bits;
    slot.width = width;
    return true;
}

bool Lowering::evalFileStmts(const std::vector<StmtP>& stmts, FileVal& result, bool& returned,
                             bool& exited, FileVals& vals, FileReaders& files) {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        const Stmt& stmt = *stmtp;
        if (stmt.kind == Stmt::RETURN) {
            if (!stmt.value || !evalFileExpr(*stmt.value, result, vals, files)) return false;
            returned = true;
            return true;
        }
        if (stmt.kind == Stmt::VAR_ASSIGN) {
            FileVal value;
            if (!stmt.value || !evalFileExpr(*stmt.value, value, vals, files)) return false;
            if (!fileAssign(stmt, value, vals, files)) return false;
            continue;
        }
        if (stmt.kind == Stmt::CALL) {
            if (!fileCall(stmt, vals, files)) return false;
            continue;
        }
        if (stmt.kind == Stmt::FOR) {
            FileVal lo;
            FileVal hi;
            if (!stmt.b || !stmt.c || !evalFileExpr(*stmt.b, lo, vals, files)
                || !evalFileExpr(*stmt.c, hi, vals, files)) {
                unsupportedError(stmt.flp, "loop in a subprogram that reads a file");
                return false;
            }
            for (int64_t k = lo.i; stmt.rangeAscending ? k <= hi.i : k >= hi.i;
                 stmt.rangeAscending ? ++k : --k) {
                FileVal& loopVar = vals[stmt.loopVar];
                loopVar = FileVal{};
                loopVar.i = k;
                bool bodyReturned = false;
                bool bodyExited = false;
                if (!evalFileStmts(stmt.body, result, bodyReturned, bodyExited, vals, files)) {
                    return false;
                }
                if (bodyReturned) {
                    returned = true;
                    return true;
                }
                if (bodyExited) break;
            }
            continue;
        }
        if (stmt.kind == Stmt::EXIT) {
            FileVal cond;
            cond.i = 1;
            if (stmt.value && !evalFileExpr(*stmt.value, cond, vals, files)) return false;
            if (cond.i) {
                exited = true;
                return true;
            }
            continue;
        }
        if (stmt.kind == Stmt::REPORT || stmt.kind == Stmt::ASSERT) continue;
        unsupportedError(stmt.flp, "statement in a subprogram that reads a file");
        return false;
    }
    return true;
}

AstNodeExpr* Lowering::evalFileInit(const SubprogramDecl& subp, const Expr& call) {
    FileVals vals;
    FileReaders files;
    // Bind the formals: a string one takes the actual's text, the rest its value
    for (size_t i = 0; i < subp.formals.size() && i < call.args.size(); ++i) {
        const FormalDecl& formal = subp.formals[i];
        FileVal value;
        bool known = true;
        if (formal.type.mark == "string") {
            if (call.args[i]->kind == Expr::STRING_LIT) {
                value.text = call.args[i]->spelling;
            } else if (call.args[i]->kind == Expr::NAME) {
                const auto textIt = m_stringConsts.find(call.args[i]->text);
                if (textIt != m_stringConsts.end()) value.text = textIt->second;
                known = !value.text.empty();
            } else {
                known = false;
            }
        } else {
            known = evalFileExpr(*call.args[i], value, vals, files);
        }
        if (!known) {
            unsupportedError(call.args[i]->flp, "argument of a subprogram that reads a file");
            return nullptr;
        }
        for (const auto& name : formal.names) vals[name.first] = value;
    }
    // The locals, so a read knows how wide the value it fills is
    for (const Decl& decl : subp.decls) {
        if (decl.kind == Decl::FILE) {
            std::string name;
            if (decl.init && decl.init->kind == Expr::STRING_LIT) {
                name = decl.init->spelling;
            } else if (decl.init && decl.init->kind == Expr::NAME) {
                const auto bound = vals.find(decl.init->text);
                if (bound != vals.end()) name = bound->second.text;
                if (name.empty()) {
                    const auto textIt = m_stringConsts.find(decl.init->text);
                    if (textIt != m_stringConsts.end()) name = textIt->second;
                }
            }
            if (name.empty()) {
                unsupportedError(decl.flp, "file name that is not a string constant");
                return nullptr;
            }
            if (!decl.fileMode.empty() && decl.fileMode != "read"
                && decl.fileMode != "read_mode") {
                unsupportedError(decl.flp, "file opened for writing");
                return nullptr;
            }
            for (const auto& one : decl.names) {
                FileReader reader;
                reader.in.open(name);
                if (!reader.in.is_open()) {
                    unsupportedError(decl.flp, "cannot open file '" + name + "'");
                    return nullptr;
                }
                files.emplace(one.first, std::move(reader));
            }
            continue;
        }
        VType declType;
        if (!resolveType(decl.type, declType)) return nullptr;
        for (const auto& one : decl.names) {
            FileVal& slot = vals[one.first];
            slot.width = declType.width;
            if (slot.bits.empty())
                slot.bits = std::string(static_cast<size_t>(declType.width), '0');
        }
    }
    VType retType;
    if (!resolveType(subp.returnType, retType)) return nullptr;
    FileVal result;
    bool returned = false;
    bool exited = false;
    if (!evalFileStmts(subp.body, result, returned, exited, vals, files)) return nullptr;
    if (!returned) {
        unsupportedError(call.flp, "subprogram that reads a file returns no image");
        return nullptr;
    }
    if (!result.elems.empty()) {
        // An array of elements: indexOffset says where each one sits, counted in
        // elements from the least significant
        if (!retType.isVector() || retType.elemWidth <= 0) {
            unsupportedError(call.flp, "image that is not an array of vectors");
            return nullptr;
        }
        std::string bits(static_cast<size_t>(retType.width), '0');
        for (const auto& elem : result.elems) {
            const int offset = retType.indexOffset(static_cast<int>(elem.first));
            if (offset < 0 || offset >= retType.length()) {
                unsupportedError(call.flp, "image element outside the array bounds");
                return nullptr;
            }
            const size_t begin = bits.size() - static_cast<size_t>(offset + 1) * retType.elemWidth;
            const size_t count
                = std::min(elem.second.size(), static_cast<size_t>(retType.elemWidth));
            std::copy(elem.second.end() - static_cast<std::ptrdiff_t>(count), elem.second.end(),
                      bits.begin()
                          + static_cast<std::ptrdiff_t>(begin + retType.elemWidth - count));
        }
        result.bits = std::move(bits);
    }
    if (static_cast<int>(result.bits.size()) < retType.width) {
        result.bits.insert(result.bits.begin(), retType.width - result.bits.size(), '0');
    }
    return makeBitsConst(call.flp, retType.dtp, result.bits);
}

//! The visible reference to a subprogram declaration, or nullptr if it is hidden
const Lowering::SubprogramRef* Lowering::visibleRef(const SubprogramDecl* declp) const {
    for (const auto& entry : m_subprograms) {
        for (const SubprogramRef& ref : entry.second) {
            if (ref.declp == declp) return &ref;
        }
    }
    return nullptr;
}

//! Bind an inlined function's aliases, local types and local variables into its
//! environment; false if one of them could not be lowered
bool Lowering::bindFunctionLocals(const SubprogramDecl& subp, InlineEnv& env) {
    // A local type may name a constant of the same declarative part, so the types register
    // in source order here, once the constants before them are bound
    std::vector<const TypeDecl*> pendingTypes;
    for (const TypeDecl& typeDecl : subp.types) pendingTypes.push_back(&typeDecl);
    const auto registerTypesBefore = [this, &pendingTypes](int lineno) {
        for (auto it = pendingTypes.begin(); it != pendingTypes.end();) {
            if ((*it)->flp->lineno() > lineno) {
                ++it;
                continue;
            }
            registerType(**it);
            it = pendingTypes.erase(it);
        }
    };
    registerSubprograms(subp.subprograms, static_cast<int>(m_inlineFrames.size() - 1));
    // An alias denotes its designator; its subtype mark gives it bounds of its own. An
    // alias with a signature renames a subprogram, so it is registered as one instead of
    // being valued like an object alias would be
    for (const AliasDecl& alias : subp.aliases) {
        if (!alias.designator) continue;
        if (alias.hasSignature) {
            registerSubprogramAlias(alias);
            continue;
        }
        AstNodeExpr* const valuep = lowerExpr(*alias.designator, nullptr);
        if (!valuep) return false;
        InlineBinding binding{valuep, false, true, VType{}, nullptr};
        VType aliasType;
        if (alias.mark.flp && resolveType(alias.mark, aliasType)
            && aliasType.width == valuep->width()) {
            valuep->dtypep(aliasType.dtp);
            binding.type = aliasType;
        } else {
            binding.type = typeOfDType(valuep->dtypep());
        }
        env[alias.name] = std::move(binding);
    }
    // Locals are bound to values; assignments rebuild the whole bound expression
    for (const Decl& decl : subp.decls) {
        registerTypesBefore(decl.flp->lineno());
        VType declType;
        const bool resolved = resolveType(decl.type, declType);
        AstNodeExpr* initp = nullptr;
        if (decl.init) {
            initp = lowerExpr(*decl.init, resolved ? &declType : nullptr);
        } else if (resolved) {
            initp = makeVectorConst(decl.flp, declType, 0);
        }
        if (!initp) return false;
        // An element write rebuilds an array local's whole value, and each read of the
        // local copies that value into the rebuild, so its expression can grow by a
        // factor per write (open-logic's ppcOr). Where the call has a statement sink the
        // local becomes a real variable and its writes are statements.
        const bool materialize = m_materializeLocals && decl.kind == Decl::VARIABLE && resolved
                                 && declType.kind == VType::ARRAY && declType.dtp;
        bool first = true;
        for (const auto& name : decl.names) {
            if (materialize) {
                AstVar* const varp = new AstVar{
                    decl.flp, VVarType::VAR,
                    "__Vfn_" + std::to_string(++m_localVarSeq) + "__" + name.first, declType.dtp};
                m_modp->addStmtsp(varp);
                addPendingStmt(
                    new AstAssign{decl.flp, new AstVarRef{decl.flp, varp, VAccess::WRITE},
                                  resizeExpr(initp->cloneTree(false), declType, decl.flp)});
                InlineBinding binding{new AstVarRef{decl.flp, varp, VAccess::READ}, true, true,
                                      VType{}, nullptr};
                binding.type = declType;
                binding.materializedp = varp;
                env[name.first] = std::move(binding);
                continue;
            }
            InlineBinding binding{first ? initp : initp->cloneTree(false), false, true, VType{},
                                  nullptr};
            binding.type = resolved ? declType : typeOfDType(binding.exprp->dtypep());
            env[name.first] = std::move(binding);
            first = false;
        }
        if (materialize) initp->deleteTree();
    }
    // A type no declaration follows still belongs to the part
    for (const TypeDecl* const typeDeclp : pendingTypes) registerType(*typeDeclp);
    return true;
}

//! Keep the array structure of a returned value, so an unconstrained return type can take
//! its bounds from the body: `return ret` of a constrained local
void Lowering::noteReturnedType(InlineFrame& frame, const Expr& value, AstNodeExpr* valuep) {
    if (frame.returnedTypeMixed) return;
    const VType type = typeOfLowered(value, valuep);
    if (!type.isVector() || !type.dtp || type.width != valuep->width()) return;
    if (frame.hasReturnedType
        && (frame.returnedType.left != type.left || frame.returnedType.right != type.right
            || frame.returnedType.ascending != type.ascending)) {
        frame.returnedTypeMixed = true;
        return;
    }
    frame.returnedType = type;
    frame.hasReturnedType = true;
}

//! Give an inlined call's result the declared return type, or the caller's type when the
//! widths agree, and record the element structure of an array result
AstNodeExpr* Lowering::adoptCallResultType(AstNodeExpr* resultp, const SubprogramDecl& subp,
                                           const VType& retType, const VType* contextp,
                                           FileLine* flp) {
    if (retType.dtp && retType.width > 0) {
        // The return expression was lowered without the return type; normalize it so an
        // enum-backed std_ulogic adopts the one-bit logic type and vice versa
        if (resultp->width() == retType.width) {
            resultp->dtypep(retType.dtp);
        } else {
            resultp = resizeExpr(resultp, retType, flp);
        }
    }
    if (contextp && contextp->dtp && resultp->width() == contextp->width) {
        resultp->dtypep(contextp->dtp);
    }
    // A constrained array return type keeps the element structure that the packed dtype
    // would otherwise lose; comparisons of a direct call need it. An unconstrained return
    // takes the element type from the declaration and the element count from the result.
    VType structType;
    bool haveStruct = retType.kind == VType::ARRAY && retType.elemWidth > 0;
    const InlineFrame& frame = m_inlineFrames.back();
    // A return type without bounds of its own takes the ones the body returned
    if ((!haveStruct || retType.width == 0) && frame.hasReturnedType && !frame.returnedTypeMixed
        && frame.returnedType.width == resultp->width()) {
        m_arrayDtypes[resultp->dtypep()] = frame.returnedType;
        return resultp;
    }
    if (haveStruct) {
        structType = retType;
    } else {
        const auto typeIt = m_types.find(subp.returnType.mark);
        if (typeIt != m_types.end() && typeIt->second.kind == VType::ARRAY
            && typeIt->second.elemWidth > 0) {
            structType = typeIt->second;
            haveStruct = true;
        }
    }
    if (haveStruct && resultp->width() % structType.elemWidth == 0) {
        structType.width = resultp->width();
        const int elements = structType.width / structType.elemWidth;
        if (elements > 0 && structType.length() != elements) {
            // Packed order: the first element is the most significant one
            structType.left = elements - 1;
            structType.right = 0;
            structType.ascending = false;
        }
        m_arrayDtypes[resultp->dtypep()] = structType;
    }
    return resultp;
}

AstNodeExpr* Lowering::lowerFunctionCall(const Expr& call, const VType* contextp,
                                         const SubprogramDecl* forcedp) {
    const SubprogramRef* refp = nullptr;
    const SubprogramDecl* subp = nullptr;
    if (forcedp) {
        subp = forcedp;
        // The forced declaration came from visibleSubprograms, which reads the same table
        refp = visibleRef(forcedp);
        UASSERT(refp, "Forced operator declaration is not visible");
    } else {
        subp = resolveCall(call, true, &refp, contextp);
        if (!subp) return nullptr;  // resolveCall reported why
    }
    // A subprogram that reads a file builds its result at elaboration
    if (readsFile(*subp)) return evalFileInit(*subp, call);
    m_funcWriteCost = 0;  // This call's own fold budget
    InlineEnv env;
    AstNodeStmt* unusedPrologsp = nullptr;  // A function has no out mode formal
    // The captured environment supplies enclosing names; formals bind on top of them
    ScopeSave base;
    captureBaseFor(*refp, base, env);
    if (!bindSubprogramFormals(call, *subp, env, unusedPrologsp, false, false)) {
        deleteInlineEnv(env);
        return nullptr;
    }
    // An actual may call this same subprogram again: nesting is not recursion, so the body
    // is marked only once its actuals are bound
    if (!m_inlineBodies.insert(subp).second) {
        deleteInlineEnv(env);
        unsupportedError(call.flp, "recursive call of function '" + subp->spelling + "'");
        return nullptr;
    }
    const ScopeSave outer = saveScope();
    InlineEnv* const savedInlinep = m_inlinep;
    m_inlineFrames.push_back(InlineFrame{subp, outer, ScopeSave{}, &env});
    setScope(base);
    // A body a package declares sees that package's whole scope: the caller may have
    // imported only the specification
    if (!subp->unitName.empty() && subp->unitName != m_unitName) {
        importInlineScope(subp->unitName);
    }
    m_inlinep = &env;
    const auto finish = [&](AstNodeExpr* nullp) -> AstNodeExpr* {
        deleteInlineEnv(env);
        m_inlineFrames.pop_back();
        m_inlinep = savedInlinep;
        restoreScope(outer);
        m_inlineBodies.erase(subp);
        return nullp;
    };

    if (!bindFunctionLocals(*subp, env)) return finish(nullptr);
    VType retType;
    const VType* const outerRetp = m_funcRetp;
    // An unconstrained array return type takes its width from the body and the context
    const bool retSized = !isUnconstrainedMark(subp->returnType.mark) || subp->returnType.hasRange
                          || !subp->returnType.constraintMark.empty();
    // An unconstrained return type takes its width from the body, and a body that needs
    // one (a conversion whose width is a run-time value) takes the caller's type
    m_funcRetp = retSized && resolveType(subp->returnType, retType) ? &retType : contextp;
    const InlineCont noCont = [](InlineEnv&) -> AstNodeExpr* { return nullptr; };
    AstNodeExpr* resultp = evalFuncBlock(subp->body, 0, env, noCont, nullptr, 0);
    m_funcRetp = outerRetp;
    if (resultp) resultp = adoptCallResultType(resultp, *subp, retType, contextp, call.flp);
    return finish(resultp);
}

AstNodeExpr* Lowering::evalFuncBlock(const std::vector<StmtP>& stmts, std::size_t index,
                                     InlineEnv& env, const InlineCont& cont,
                                     const InlineCont* exitContp, int depth) {
    InlineEnv* const savedInlinep = m_inlinep;
    m_inlinep = &env;
    if (depth > 64) {
        unsupportedError(m_inlineFrames.back().declp->flp,
                         "VHDL function statement nesting is too deep");
        m_inlinep = savedInlinep;
        return nullptr;
    }
    AstNodeExpr* const resultp = evalFuncBlockBody(stmts, index, cont, exitContp, depth);
    m_inlinep = savedInlinep;
    return resultp;
}

//! Assign a local of an inlined function body: a whole local takes the new value, an
//! element rebuilds the local's value around it; false if the assignment cannot fold
bool Lowering::evalFuncVarAssign(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    const Expr& target = *stmt.target;
    if (target.kind != Expr::NAME) return evalFuncElementAssign(stmt);
    InlineEnv& bindings = activeEnv();
    const auto it = bindings.find(target.text);
    if (it == bindings.end()) {
        unsupportedError(flp, "assignment to a name that is not local to the inlined subprogram");
        return false;
    }
    // The target constrains the value, e.g. an aggregate's width
    AstNodeExpr* const valuep
        = lowerExpr(*stmt.value, it->second.type.dtp ? &it->second.type : nullptr);
    if (!valuep) return false;
    if (it->second.materializedp) {
        // The evaluator lowers both sides of a run-time branch, so a
        // statement here would run on both paths
        if (m_runtimeBranchDepth > 0) {
            unsupportedError(flp, "array local assigned under a run-time condition");
            return false;
        }
        // A real variable takes the assignment as a statement
        addPendingStmt(new AstAssign{flp,
                                     new AstVarRef{flp, it->second.materializedp, VAccess::WRITE},
                                     resizeExpr(valuep, it->second.type, flp)});
        return true;
    }
    it->second.exprp->deleteTree();
    it->second.exprp = valuep;
    return true;
}

//! Assign one element of a local of an inlined function body
bool Lowering::evalFuncElementAssign(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    const Expr& target = *stmt.target;
    // Locals are bound to values: rebuild the whole value around the element update
    const Expr* basep = nullptr;
    for (const Expr* prefixp = &target;;) {
        if (prefixp->kind == Expr::NAME) {
            basep = prefixp;
            break;
        }
        // A designator's own parts nest the same way the lowered selects do
        if ((prefixp->kind != Expr::INDEX && prefixp->kind != Expr::SLICE
             && prefixp->kind != Expr::SELECTED)
            || !prefixp->a) {
            break;
        }
        prefixp = prefixp->a.get();
    }
    if (!basep) {
        unsupportedError(flp, "assignment target form in an inlined function body");
        return false;
    }
    InlineEnv& bindings = activeEnv();
    const auto it = bindings.find(basep->text);
    if (it == bindings.end()) {
        unsupportedError(flp, "assignment to an element outside the inlined subprogram");
        return false;
    }
    if (it->second.materializedp) {
        // The evaluator lowers both sides of a run-time branch, so a statement
        // here would run on both paths
        if (m_runtimeBranchDepth > 0) {
            unsupportedError(flp, "array local assigned under a run-time condition");
            return false;
        }
        // A real variable takes one element assignment as one statement, with no
        // rebuild of the value around it
        AstNodeExpr* const lhsp = lowerDesignator(target, true);
        if (!lhsp) return false;
        // The declared type keeps a record element's fields; the lowered lvalue is
        // the packed vector, which would turn `r(i) := (a, b, c)` into an array
        // aggregate that covers no field
        const VType elemType = typeOfTarget(target, lhsp);
        AstNodeExpr* const valuep = lowerExpr(*stmt.value, &elemType);
        if (!valuep) {
            lhsp->deleteTree();
            return false;
        }
        addPendingStmt(new AstAssign{flp, lhsp, resizeExpr(valuep, elemType, flp)});
        return true;
    }
    // Each element write rebuilds the local's whole value, and a body that reads
    // the local back grows the rebuilt tree by that read too. The budget follows
    // the value's size, which is what every write copies
    m_funcWriteCost += 64 + it->second.exprp->width() / 64 + it->second.exprp->nodeCount();
    if (m_funcWriteCost > MAX_FOLD_WRITE_COST) {
        unsupportedError(flp, "function body with too many element assignments");
        return false;
    }
    AstNodeExpr* const readp = lowerDesignator(target, false);
    AstSel* const selp = VN_CAST(readp, Sel);
    if (!selp) {
        if (readp) readp->deleteTree();
        unsupportedError(flp, "assignment to an element of a non-vector value");
        return false;
    }
    // A nested target selects inside a select, so its offset in the local's value
    // is the sum of the levels: `v(i)(j)` lands inside `v`. The selects that came
    // from binding the local to its actual are not part of the target -- the
    // local's own value is what is rebuilt -- so summing stops at the level whose
    // width is the local's
    const int localWidth = it->second.exprp->width();
    AstNodeExpr* offsetp = selp->lsbp()->cloneTree(false);
    for (AstSel* subjectp = VN_CAST(selp->fromp(), Sel);
         subjectp && subjectp->width() != localWidth; subjectp = VN_CAST(subjectp->fromp(), Sel)) {
        AstNodeExpr* const summed = new AstAdd{flp, offsetp, subjectp->lsbp()->cloneTree(false)};
        summed->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
        offsetp = summed;
    }
    // The declared element type keeps a record's fields; the lowered select is the
    // packed vector, which would turn `r(i) := (a, b, c)` into an array aggregate
    VType elemType;
    if (!typeOfDesignator(target, elemType)) elemType = typeOfDType(selp->dtypep());
    AstNodeExpr* valuep = lowerExpr(*stmt.value, &elemType);
    if (!valuep) {
        readp->deleteTree();
        offsetp->deleteTree();
        return false;
    }
    AstNodeExpr* const updatedp
        = updateLocalElement(flp, it->second.exprp->cloneTree(false), valuep, selp->dtypep(),
                             offsetp, selp->widthConst());
    readp->deleteTree();
    it->second.exprp->deleteTree();
    it->second.exprp = updatedp;
    return true;
}

//! An exit or next statement of an inlined function body; a conditional one selects
//! between its target and the rest of the body
AstNodeExpr* Lowering::evalFuncExitNext(const std::vector<StmtP>& stmts, std::size_t i,
                                        const InlineCont& cont, const InlineCont* exitContp,
                                        int depth) {
    const Stmt& stmt = *stmts[i];
    FileLine* const flp = stmt.flp;
    AstNodeExpr* const condExprp = stmt.value ? lowerCond(*stmt.value) : nullptr;
    if (stmt.value && !condExprp) return nullptr;
    AstNodeExpr* targetp = nullptr;
    // A conditional exit lowers the rest of the body as well, and only one of
    // the two paths runs
    const RuntimeBranchScope exitScope{stmt.value ? m_runtimeBranchDepth : m_unusedDepth};
    if (!stmt.loopLabel.empty()) {
        // A label selects that active loop; the innermost match wins
        const InlineCont* foundp = nullptr;
        for (std::size_t t = m_funcLoopTargets.size(); t-- > 0;) {
            if (m_funcLoopTargets[t].label != stmt.loopLabel) continue;
            foundp = stmt.kind == Stmt::EXIT ? m_funcLoopTargets[t].exitp
                                             : m_funcLoopTargets[t].nextp;
            break;
        }
        if (!foundp) {
            unsupportedError(flp, "exit/next to a loop that is not visible here");
            if (condExprp) condExprp->deleteTree();
            return nullptr;
        }
        targetp = (*foundp)(activeEnv());
    } else if (stmt.kind == Stmt::EXIT) {
        if (!exitContp) {
            unsupportedError(flp, "exit statement outside a static loop in a function");
            if (condExprp) condExprp->deleteTree();
            return nullptr;
        }
        targetp = (*exitContp)(activeEnv());
    } else {
        targetp = cont(activeEnv());
    }
    if (!condExprp) return targetp;
    // The statements after a conditional exit/next only run when it is not taken
    AstNodeExpr* restp = evalFuncBlockBody(stmts, i + 1, cont, exitContp, depth);
    if (!targetp || !restp) {
        if (targetp) targetp->deleteTree();
        if (restp) restp->deleteTree();
        return nullptr;
    }
    if (targetp->width() != restp->width()) {
        if (targetp->width() > restp->width()) {
            restp = resizeExpr(restp, typeOfDType(targetp->dtypep()), flp);
        } else {
            targetp = resizeExpr(targetp, typeOfDType(restp->dtypep()), flp);
        }
    }
    AstCond* const resultp = new AstCond{flp, condExprp, targetp, restp};
    resultp->dtypep(targetp->dtypep());
    return resultp;
}

AstNodeExpr* Lowering::evalFuncBlockBody(const std::vector<StmtP>& stmts, std::size_t index,
                                         const InlineCont& cont, const InlineCont* exitContp,
                                         int depth) {
    for (std::size_t i = index; i < stmts.size(); ++i) {
        const Stmt& stmt = *stmts[i];
        FileLine* const flp = stmt.flp;
        // A conditional waveform cannot be folded to a single value here; lower it at run time
        if ((stmt.kind == Stmt::ASSIGN || stmt.kind == Stmt::VAR_ASSIGN)
            && !stmt.alternatives.empty()) {
            return nullptr;
        }
        if (stmt.kind == Stmt::VAR_ASSIGN) {
            if (!evalFuncVarAssign(stmt)) return nullptr;
            continue;
        }
        if (stmt.kind == Stmt::REPORT || stmt.kind == Stmt::ASSERT) continue;
        if (stmt.kind == Stmt::RETURN) {
            if (!stmt.value) {
                unsupportedError(flp, "return without a value in a VHDL function body");
                return nullptr;
            }
            AstNodeExpr* const valuep = lowerExpr(*stmt.value, m_funcRetp);
            if (!valuep) return nullptr;
            if (!m_inlineFrames.empty()) {
                noteReturnedType(m_inlineFrames.back(), *stmt.value, valuep);
                VType retType;
                if (formalType(m_inlineFrames.back().declp->returnType, retType)
                    && !valueConforms(retType, *stmt.value, "return value", false)) {
                    valuep->deleteTree();
                    return nullptr;
                }
            }
            return valuep;
        }
        if (stmt.kind == Stmt::IF || stmt.kind == Stmt::CASE || stmt.kind == Stmt::FOR
            || stmt.kind == Stmt::WHILE) {
            // Control statements continue with the rest of this statement list
            const InlineCont restCont = [&](InlineEnv& restEnv) -> AstNodeExpr* {
                return evalFuncBlock(stmts, i + 1, restEnv, cont, exitContp, depth);
            };
            if (stmt.kind == Stmt::IF) {
                return evalFuncBranches(stmt, 0, restCont, exitContp, depth + 1);
            }
            if (stmt.kind == Stmt::CASE) return evalFuncCase(stmt, restCont, exitContp, depth + 1);
            if (stmt.kind == Stmt::WHILE) return evalFuncWhile(stmt, restCont, depth + 1);
            return evalFuncFor(stmt, restCont, depth + 1);
        }
        if (stmt.kind == Stmt::EXIT || stmt.kind == Stmt::NEXT) {
            return evalFuncExitNext(stmts, i, cont, exitContp, depth);
        }
        unsupportedError(flp, "this statement form in an inlined VHDL function body");
        return nullptr;
    }
    AstNodeExpr* const resultp = cont(activeEnv());
    // Only a body that really has no value on some path is a missing return; a null from a
    // nested fold already has its own diagnostic
    if (!resultp && !V3Error::errorCount()) {
        unsupportedError(m_inlineFrames.back().declp->flp,
                         "function does not return a value on all paths");
    }
    return resultp;
}

AstNodeExpr* Lowering::evalFuncBranches(const Stmt& stmt, std::size_t branchIndex,
                                        const InlineCont& cont, const InlineCont* exitContp,
                                        int depth) {
    const auto& branch = stmt.branches[branchIndex];
    if (!branch.first) {
        // The final else continues through the rest of the enclosing block
        return evalFuncBlock(branch.second, 0, activeEnv(), cont, exitContp, depth);
    }
    AstNodeExpr* const condp = lowerCond(*branch.first);
    if (!condp) return nullptr;
    // A condition that folds to a constant selects the path taken, so the one not taken
    // needs no value -- which is what lets a body return from inside a static loop
    if (AstConst* const foldedp = foldStatic(condp)) {
        const bool taken = foldedp->num().isTrue();
        foldedp->deleteTree();
        condp->deleteTree();
        if (taken) {
            InlineEnv thenEnv = cloneInlineEnv(activeEnv());
            AstNodeExpr* const takenp
                = evalFuncBlock(branch.second, 0, thenEnv, cont, exitContp, depth);
            deleteInlineEnv(thenEnv);
            return takenp;
        }
        return branchIndex + 1 < stmt.branches.size()
                   ? evalFuncBranches(stmt, branchIndex + 1, cont, exitContp, depth)
                   : evalFuncBlock({}, 0, activeEnv(), cont, exitContp, depth);
    }
    InlineEnv thenEnv = cloneInlineEnv(activeEnv());
    AstNodeExpr* thenp = nullptr;
    AstNodeExpr* elsep = nullptr;
    {
        // Both paths are lowered here, so a statement one of them emits would run on
        // both
        const RuntimeBranchScope scope{m_runtimeBranchDepth};
        thenp = evalFuncBlock(branch.second, 0, thenEnv, cont, exitContp, depth);
        elsep = branchIndex + 1 < stmt.branches.size()
                    ? evalFuncBranches(stmt, branchIndex + 1, cont, exitContp, depth)
                    : evalFuncBlock({}, 0, activeEnv(), cont, exitContp, depth);
    }
    deleteInlineEnv(thenEnv);
    if (!thenp || !elsep) {
        if (!V3Error::errorCount()) {
            unsupportedError(stmt.flp, "function does not return a value on all paths");
        }
        if (thenp) thenp->deleteTree();
        if (elsep) elsep->deleteTree();
        return nullptr;
    }
    if (thenp->width() != elsep->width()) {
        if (thenp->width() > elsep->width()) {
            elsep = resizeExpr(elsep, typeOfDType(thenp->dtypep()), stmt.flp);
        } else {
            thenp = resizeExpr(thenp, typeOfDType(elsep->dtypep()), stmt.flp);
        }
    }
    AstCond* const resultp = new AstCond{stmt.flp, condp, thenp, elsep};
    resultp->dtypep(thenp->dtypep());
    return resultp;
}

AstNodeExpr* Lowering::evalFuncCase(const Stmt& stmt, const InlineCont& cont,
                                    const InlineCont* exitContp, int depth) {
    AstNodeExpr* const selectorp = lowerExpr(*stmt.selector, nullptr);
    if (!selectorp) return nullptr;
    VType selType;
    if (!actualType(*stmt.selector, selType)) selType = typeOfDType(selectorp->dtypep());
    // A selector that folds to a constant picks which arm runs, so the others need no value
    AstConst* const selConstp = foldStatic(selectorp);
    const bool known = selConstp && !selConstp->num().isFourState();
    bool kept = false;
    struct Branch final {
        AstNodeExpr* condp = nullptr;  // null for the "others" alternative
        AstNodeExpr* resultp = nullptr;  // Lowered value of the alternative
        InlineEnv env;  // Bindings in effect while lowering the value
    };
    std::vector<Branch> branches;
    const auto cleanup = [&]() {
        for (Branch& branch : branches) {
            if (branch.condp) branch.condp->deleteTree();
            if (branch.resultp) branch.resultp->deleteTree();
            deleteInlineEnv(branch.env);
        }
    };
    AstNodeDType* const bitdtp = findBasic(stmt.flp, VBasicDTypeKwd::LOGIC);
    std::set<int64_t> covered;
    bool bad = false;
    for (const auto& item : stmt.caseItems) {
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(item.first, selectorp->dtypep(), cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            if (selConstp) selConstp->deleteTree();
            cleanup();
            selectorp->deleteTree();
            return nullptr;
        }
        // An exhaustive case needs no fallthrough value, as in a lowered case statement
        if (!known && !cops.empty() && !addChoiceValues(cops, covered, "case statement")) {
            bad = true;
        }
        if (known && kept) {
            for (AstConst* const cop : cops) cop->deleteTree();
            continue;  // The first matching choice wins
        }
        bool matches = !known || cops.empty();
        for (AstConst* const cop : cops) {
            if (known && cop->num().isCaseEq(selConstp->num())) matches = true;
        }
        if (!matches) {
            for (AstConst* const cop : cops) cop->deleteTree();
            continue;
        }
        kept = true;
        Branch branch;
        branch.env = cloneInlineEnv(activeEnv());
        {
            // Every matching arm is lowered, so an arm's statements would all run
            const RuntimeBranchScope scope{m_runtimeBranchDepth};
            branch.resultp = evalFuncBlock(item.second, 0, branch.env, cont, exitContp, depth);
        }
        if (known) {
            for (AstConst* const cop : cops) cop->deleteTree();
        } else if (!cops.empty()) {
            AstNodeExpr* condp = new AstEq{stmt.flp, selectorp->cloneTree(false), cops[0]};
            condp->dtypep(bitdtp);
            for (std::size_t i = 1; i < cops.size(); ++i) {
                AstNodeExpr* const eqp = new AstEq{stmt.flp, selectorp->cloneTree(false), cops[i]};
                eqp->dtypep(bitdtp);
                AstNodeExpr* const orp = new AstOr{stmt.flp, condp, eqp};
                orp->dtypep(bitdtp);
                condp = orp;
            }
            branch.condp = condp;
        }
        branches.push_back(std::move(branch));
    }
    if (selConstp) selConstp->deleteTree();
    if (known && branches.empty()) return cont(activeEnv());  // No choice matched, no 'others'
    AstNodeExpr* resultp = nullptr;
    for (Branch& branch : branches) {
        if (branch.condp) continue;
        resultp = branch.resultp;
        branch.resultp = nullptr;
        break;
    }
    if (!resultp && !known && !bad && choicesComplete(selectorp->dtypep(), selType, covered)) {
        // Every value of the selector is covered, so no fallthrough value is ever used;
        // any arm's value serves as the chain's default
        for (Branch& branch : branches) {
            if (!branch.resultp) continue;
            resultp = branch.resultp->cloneTree(false);
            break;
        }
    }
    if (!resultp) {
        if (known) {
            cleanup();
            selectorp->deleteTree();
            return nullptr;  // The arm that runs has no value on its path
        }
        // No `others` arm: a selector that matches nothing leaves the rest of the body to
        // run with the values it had before the case (IEEE 1076-2008 10.9)
        InlineEnv preEnv = cloneInlineEnv(activeEnv());
        resultp = cont(preEnv);
        deleteInlineEnv(preEnv);
        if (!resultp) {
            cleanup();
            selectorp->deleteTree();
            return nullptr;
        }
    }
    for (std::size_t i = branches.size(); i-- > 0;) {
        Branch& branch = branches[i];
        if (!branch.condp) continue;
        if (!branch.resultp) {
            resultp->deleteTree();
            cleanup();
            selectorp->deleteTree();
            return nullptr;  // evalFuncBlock reported the missing path
        }
        if (branch.resultp->width() != resultp->width()) {
            if (branch.resultp->width() > resultp->width()) {
                resultp = resizeExpr(resultp, typeOfDType(branch.resultp->dtypep()), stmt.flp);
            } else {
                branch.resultp
                    = resizeExpr(branch.resultp, typeOfDType(resultp->dtypep()), stmt.flp);
            }
        }
        AstCond* const condp = new AstCond{stmt.flp, branch.condp, branch.resultp, resultp};
        condp->dtypep(branch.resultp->dtypep());
        branch.condp = nullptr;
        branch.resultp = nullptr;
        resultp = condp;
    }
    cleanup();
    selectorp->deleteTree();
    return resultp;
}

AstNodeExpr* Lowering::evalFuncFor(const Stmt& stmt, const InlineCont& postCont, int depth) {
    FileLine* const flp = stmt.flp;
    int64_t left = 0;
    int64_t right = 0;
    bool up = stmt.rangeAscending;
    if (!stmt.c) {
        // `for i in name'range` / `name'reverse_range`: the bounds come from the prefix type
        const Expr* const rangep = stmt.b.get();
        if (!rangep || rangep->kind != Expr::ATTR || !rangep->a
            || (rangep->text != "range" && rangep->text != "reverse_range")) {
            unsupportedError(flp, "function loop range without static bounds");
            return nullptr;
        }
        VType bounds;
        if (!typeOfDesignator(*rangep->a, bounds)) {
            unsupportedError(flp, "function loop range of an unresolved name");
            return nullptr;
        }
        const VType* const levelp = indexRangeLevel(*rangep, bounds);
        if (!levelp) {
            unsupportedError(flp, "function loop range of an unknown index");
            return nullptr;
        }
        left = levelp->left;
        right = levelp->right;
        up = levelp->ascending;
        if (rangep->text == "reverse_range") {
            std::swap(left, right);
            up = !up;
        }
    } else if (!staticIntFold(stmt.b, left) || !staticIntFold(stmt.c, right)) {
        // A bound may be a local constant whose initializer folds only through a call
        unsupportedError(flp, "function loop range that is not static");
        return nullptr;
    }
    if (up ? right < left : right > left) return postCont(activeEnv());  // Empty range
    const int64_t step = up ? 1 : -1;
    // Each iteration becomes a continuation frame; the body's continuation is the next
    std::function<AstNodeExpr*(int64_t, InlineEnv&)> evalIteration;
    evalIteration = [&](int64_t value, InlineEnv& inEnv) -> AstNodeExpr* {
        // The cap is on the iterations actually folded: a body that returns leaves the rest
        // of the range unevaluated, so a wide range can still be a short loop
        if (std::llabs(value - left) >= MAX_FOLD_ITERATIONS) {
            unsupportedError(flp, "function loop with more than "
                                      + std::to_string(MAX_FOLD_ITERATIONS) + " iterations");
            return nullptr;
        }
        InlineEnv iterEnv = cloneInlineEnv(inEnv);
        InlineBinding loopBinding{makeIntConst(flp, value), false, true, VType{}, nullptr};
        loopBinding.type = intType(flp);
        iterEnv[stmt.loopVar] = std::move(loopBinding);
        const int64_t nextValue = value + step;
        const bool more = up ? nextValue <= right : nextValue >= right;
        const InlineCont bodyCont = [&](InlineEnv& bodyEnv) -> AstNodeExpr* {
            return more ? evalIteration(nextValue, bodyEnv) : postCont(bodyEnv);
        };
        const InlineCont exitCont
            = [&](InlineEnv& exitEnv) -> AstNodeExpr* { return postCont(exitEnv); };
        // The continuations live in this iteration's frame. Only a labeled exit or next
        // in the body can use one, and the body returns before the frame is popped.
        m_funcLoopTargets.push_back(FuncLoopTarget{stmt.loopLabel, &bodyCont, &exitCont});
        AstNodeExpr* const resultp
            = evalFuncBlock(stmt.body, 0, iterEnv, bodyCont, &exitCont, depth);
        m_funcLoopTargets.pop_back();
        deleteInlineEnv(iterEnv);
        return resultp;
    };
    return evalIteration(left, activeEnv());
}

AstNodeExpr* Lowering::evalFuncWhile(const Stmt& stmt, const InlineCont& postCont, int depth) {
    FileLine* const flp = stmt.flp;
    // One frame per iteration, like a for loop; the difference is that the trip count comes
    // from repeatedly evaluating the condition
    std::function<AstNodeExpr*(int, InlineEnv&)> evalIteration;
    evalIteration = [&](int iteration, InlineEnv& inEnv) -> AstNodeExpr* {
        AstNodeExpr* const condp = lowerCond(*stmt.value);
        if (!condp) return nullptr;
        // The condition has to say whether the loop runs at all, or the fold cannot know
        // how many times the body is applied
        AstConst* const foldedp = foldStatic(condp);
        if (!foldedp) {
            condp->deleteTree();
            unsupportedError(flp, "function while loop whose condition is not static");
            return nullptr;
        }
        const bool taken = foldedp->num().isTrue();
        foldedp->deleteTree();
        condp->deleteTree();
        if (!taken) return postCont(inEnv);
        if (iteration >= MAX_FOLD_ITERATIONS) {
            unsupportedError(flp, "function while loop with more than "
                                      + std::to_string(MAX_FOLD_ITERATIONS) + " iterations");
            return nullptr;
        }
        InlineEnv bodyEnv = cloneInlineEnv(inEnv);
        // Running to the end of the body re-tests the condition; `exit` leaves the loop
        const InlineCont bodyCont = [&](InlineEnv& nextEnv) -> AstNodeExpr* {
            return evalIteration(iteration + 1, nextEnv);
        };
        const InlineCont exitCont
            = [&](InlineEnv& exitEnv) -> AstNodeExpr* { return postCont(exitEnv); };
        // The continuations live in this iteration's frame, as for a for loop
        m_funcLoopTargets.push_back(FuncLoopTarget{stmt.loopLabel, &bodyCont, &exitCont});
        AstNodeExpr* const resultp
            = evalFuncBlock(stmt.body, 0, bodyEnv, bodyCont, &exitCont, depth);
        m_funcLoopTargets.pop_back();
        deleteInlineEnv(bodyEnv);
        return resultp;
    };
    return evalIteration(0, activeEnv());
}

//! Value of a vector-bound local after replacing one element or slice
AstNodeExpr* Lowering::updateLocalElement(FileLine* flp, AstNodeExpr* oldp, AstNodeExpr* valuep,
                                          AstNodeDType* elemDtp, AstNodeExpr* lsbp, int width) {
    const int total = oldp->dtypep()->width();
    AstNodeExpr* valp = valuep;
    if (valp->width() != width) valp = resizeExpr(valp, typeOfDType(elemDtp), flp);
    AstNodeExpr* const extendedp
        = valp->width() == total ? valp
                                 : static_cast<AstNodeExpr*>(new AstExtend{flp, valp, total});
    extendedp->dtypep(oldp->dtypep());
    AstNodeExpr* const shiftedp = new AstShiftL{flp, extendedp, lsbp->cloneTree(false), total};
    shiftedp->dtypep(oldp->dtypep());
    V3Number maskNum{flp, total, 0u};
    for (int bit = 0; bit < width && bit < total; ++bit) maskNum.setBit(bit, '1');
    AstConst* const onesp = new AstConst{flp, maskNum};
    onesp->dtypep(oldp->dtypep());
    AstNodeExpr* const maskp = new AstShiftL{flp, onesp, lsbp, total};
    maskp->dtypep(oldp->dtypep());
    AstNodeExpr* const notmaskp = new AstNot{flp, maskp->cloneTree(false)};
    notmaskp->dtypep(oldp->dtypep());
    AstNodeExpr* const clearedp = new AstAnd{flp, oldp, notmaskp};
    clearedp->dtypep(oldp->dtypep());
    AstNodeExpr* const setp = new AstAnd{flp, shiftedp, maskp};
    setp->dtypep(oldp->dtypep());
    AstNodeExpr* const resultp = new AstOr{flp, clearedp, setp};
    resultp->dtypep(oldp->dtypep());
    return resultp;
}

AstNodeStmt* Lowering::lowerProcedureCall(const Expr& callExpr, bool concurrent) {
    const SubprogramRef* refp = nullptr;
    const SubprogramDecl* const subp = resolveCall(callExpr, false, &refp);
    if (!subp) {
        const std::string name
            = callExpr.kind == Expr::INDEX && callExpr.a ? callExpr.a->text : callExpr.text;
        if (!m_subprograms.count(name)) {
            callExpr.flp->v3error("VHDL: unknown subprogram '" + name + "'.");
        }
        return nullptr;  // resolveCall reported the reason for a known subprogram
    }
    m_funcWriteCost = 0;  // This call's own fold budget
    InlineEnv env;
    AstNodeStmt* prologsp = nullptr;  // Out-parameter initializations
    // The captured environment supplies enclosing names; formals bind on top of them
    ScopeSave base;
    captureBaseFor(*refp, base, env);
    if (!bindSubprogramFormals(callExpr, *subp, env, prologsp, true, concurrent)) {
        deleteInlineEnv(env);
        if (prologsp) prologsp->deleteTree();
        return nullptr;
    }
    // An actual may call this same subprogram again: nesting is not recursion
    if (!m_inlineBodies.insert(subp).second) {
        deleteInlineEnv(env);
        if (prologsp) prologsp->deleteTree();
        unsupportedError(callExpr.flp, "recursive call of procedure '" + subp->spelling + "'");
        return nullptr;
    }
    const ScopeSave outer = saveScope();
    InlineEnv* const savedInlinep = m_inlinep;
    m_inlineFrames.push_back(InlineFrame{subp, outer, ScopeSave{}, &env});
    InlineFrame& frameRef = m_inlineFrames.back();
    frameRef.liveScope = base;
    setScope(base);
    // A body a package declares sees that package's whole scope, as a function's does
    if (!subp->unitName.empty() && subp->unitName != m_unitName) {
        importInlineScope(subp->unitName);
    }
    m_inlinep = &env;
    const auto fail = [&](AstNodeStmt* nullp) -> AstNodeStmt* {
        deleteInlineEnv(env);
        m_inlineFrames.pop_back();
        m_inlinep = savedInlinep;
        restoreScope(outer);
        m_inlineBodies.erase(subp);
        if (prologsp) prologsp->deleteTree();
        return nullp;
    };

    // A local type may name a constant of the same declarative part, so the types register
    // in source order here, once the constants before them are emitted
    std::vector<const TypeDecl*> pendingTypes;
    for (const TypeDecl& typeDecl : subp->types) pendingTypes.push_back(&typeDecl);
    const auto registerTypesBefore = [this, &pendingTypes](int lineno) {
        for (auto it = pendingTypes.begin(); it != pendingTypes.end();) {
            if ((*it)->flp->lineno() > lineno) {
                ++it;
                continue;
            }
            registerType(**it);
            it = pendingTypes.erase(it);
        }
    };
    registerSubprograms(subp->subprograms, static_cast<int>(m_inlineFrames.size() - 1));
    for (const AliasDecl& alias : subp->aliases) registerAlias(alias);

    // Locals are real module variables reinitialized on every call, like automatics
    AstNodeStmt* initsp = nullptr;
    for (const Decl& decl : subp->decls) {
        registerTypesBefore(decl.flp->lineno());
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType ltype;
        if (!resolveType(decl.type, ltype)) return fail(nullptr);
        if (rejectUnmodelledObject(ltype, decl.flp)) return fail(nullptr);
        for (const auto& name : decl.names) {
            const std::string mangled
                = "__Vproc_" + std::to_string(++m_procVarSeq) + "__" + name.first;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, ltype.dtp};
            traceDeclared(varp);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, ltype};
            m_variables.insert(name.first);
            frameRef.liveScope = saveScope();
            AstNodeExpr* rhsp = nullptr;
            if (decl.init) rhsp = lowerExpr(*decl.init, &ltype);
            if (!rhsp) rhsp = makeVectorConst(decl.flp, ltype, 0);
            AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
            AstNodeStmt* const initp = new AstAssign{decl.flp, lhsp, rhsp};
            initsp = initsp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(initsp, initp) : initp;
        }
    }
    // A type no declaration follows still belongs to the part
    for (const TypeDecl* const typeDeclp : pendingTypes) registerType(*typeDeclp);

    bool fallsThrough = true;
    AstNodeStmt* const bodysp = lowerProcChain(rawPtrs(subp->body), 0, fallsThrough);
    AstNodeStmt* resultp = prologsp;
    prologsp = nullptr;
    if (initsp)
        resultp = resultp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(resultp, initsp) : initsp;
    if (bodysp)
        resultp = resultp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(resultp, bodysp) : bodysp;
    return fail(resultp);
}

std::vector<const Stmt*> Lowering::rawPtrs(const std::vector<StmtP>& stmts) {
    std::vector<const Stmt*> out;
    out.reserve(stmts.size());
    for (const StmtP& stmtp : stmts) out.push_back(stmtp.get());
    return out;
}

//! Lower a procedure statement chain. `fallsThroughp` is false when every path returns
AstNodeStmt* Lowering::lowerProcChain(const std::vector<const Stmt*>& stmts, std::size_t index,
                                      bool& fallsThroughp) {
    m_varVals.clear();
    AstNodeStmt* headp = nullptr;
    const auto add = [&headp](AstNodeStmt* nodep) {
        if (nodep)
            headp = headp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, nodep) : nodep;
    };
    for (std::size_t i = index; i < stmts.size(); ++i) {
        const Stmt& stmt = *stmts[i];
        if (stmt.kind == Stmt::RETURN) {
            if (stmt.value) unsupportedError(stmt.flp, "return value in a procedure body");
            fallsThroughp = false;
            return headp;
        }
        if (stmt.kind == Stmt::IF) {
            AstNodeStmt* const ifp
                = lowerProcIf(stmt, 0, {stmts.begin() + i + 1, stmts.end()}, fallsThroughp);
            add(ifp);
            return headp;
        }
        if (stmt.kind == Stmt::CASE) {
            AstNodeStmt* const casep
                = lowerProcCase(stmt, {stmts.begin() + i + 1, stmts.end()}, fallsThroughp);
            add(casep);
            return headp;
        }
        if (stmt.kind == Stmt::EXIT || stmt.kind == Stmt::NEXT) {
            const bool isExit = stmt.kind == Stmt::EXIT;
            LoopFrame* const framep = findLoop(stmt.loopLabel);
            if (!framep) {
                unsupportedError(stmt.flp, isExit ? "exit outside a loop" : "next outside a loop");
                continue;
            }
            AstNodeExpr* condp = stmt.value ? lowerCond(*stmt.value) : nullptr;
            if (stmt.value && !condp) continue;
            AstNodeStmt* const setp
                = setLoopFlag(stmt.flp, isExit ? framep->exitp : framep->nextp, condp);
            add(setp);
            if (!stmt.value) {
                // An unconditional exit truncates every following statement
                fallsThroughp = false;
                return headp;
            }
            bool restFalls = true;
            AstNodeStmt* restp = lowerProcChain(stmts, i + 1, restFalls);
            if (!restp) {
                fallsThroughp = false;
                return headp;
            }
            AstNodeExpr* const notp = new AstNot{stmt.flp, condp};
            notp->dtypep(findBasic(stmt.flp, VBasicDTypeKwd::LOGIC));
            add(new AstIf{stmt.flp, notp, restp, nullptr});
            fallsThroughp = restFalls;
            return headp;
        }
        const bool savedMaterialize = m_materializeLocals;
        m_materializeLocals = true;
        AstNodeStmt* const stmtNodep = lowerStmt(stmt);
        AstNodeStmt* const pendingp = takePendingStmts();
        m_materializeLocals = savedMaterialize;
        if (pendingp) {
            add(stmtNodep ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(pendingp, stmtNodep)
                          : pendingp);
        } else {
            add(stmtNodep);
        }
    }
    fallsThroughp = true;
    return headp;
}

//! Lower the statements and then `tail`, so a return truncates the tail in that path only
AstNodeStmt* Lowering::lowerProcChainWithTail(const std::vector<StmtP>& stmts,
                                              const std::vector<const Stmt*>& tail,
                                              bool& fallsThroughp) {
    std::vector<const Stmt*> combined = rawPtrs(stmts);
    combined.insert(combined.end(), tail.begin(), tail.end());
    return lowerProcChain(combined, 0, fallsThroughp);
}

//! Lower an if/elsif/else statement, appending the following statements to each branch
AstNodeStmt* Lowering::lowerProcIf(const Stmt& stmt, std::size_t branchIndex,
                                   const std::vector<const Stmt*>& tail, bool& fallsThroughp) {
    m_varVals.clear();
    FileLine* const flp = stmt.flp;
    const auto& branch = stmt.branches[branchIndex];
    AstNodeExpr* const condp = branch.first ? lowerCond(*branch.first) : nullptr;
    if (branch.first && !condp) {
        fallsThroughp = false;
        return nullptr;
    }
    bool thenFalls = true;
    AstNodeStmt* const thensp = lowerProcChainWithTail(branch.second, tail, thenFalls);
    if (!condp) {
        // The final else has no condition, so its statements are appended as they are
        fallsThroughp = thenFalls;
        return thensp;
    }
    bool elseFalls = true;
    AstNodeStmt* elsesp = nullptr;
    if (branchIndex + 1 < stmt.branches.size()) {
        elsesp = lowerProcIf(stmt, branchIndex + 1, tail, elseFalls);
    } else {
        // A missing else falls through to the following statements
        elsesp = lowerProcChain(tail, 0, elseFalls);
    }
    fallsThroughp = thenFalls || elseFalls;
    return new AstIf{flp, condp, thensp, elsesp};
}

//! Lower a case statement, appending the following statements to each alternative
AstNodeStmt* Lowering::lowerProcCase(const Stmt& stmt, const std::vector<const Stmt*>& tail,
                                     bool& fallsThroughp) {
    m_varVals.clear();
    FileLine* const flp = stmt.flp;
    AstNodeExpr* const selectorp = lowerExpr(*stmt.selector, nullptr);
    if (!selectorp) {
        fallsThroughp = false;
        return nullptr;
    }
    AstCaseItem* itemsp = nullptr;
    fallsThroughp = false;
    for (const auto& item : stmt.caseItems) {
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(item.first, selectorp->dtypep(), cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            if (itemsp) itemsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        bool altFalls = true;
        AstNodeStmt* const bodyp = lowerProcChainWithTail(item.second, tail, altFalls);
        fallsThroughp = fallsThroughp || altFalls;
        AstCaseItem* const itemp = cops.empty() ? new AstCaseItem{flp, nullptr, bodyp}
                                                : new AstCaseItem{flp, cops.front(), bodyp};
        for (std::size_t i = 1; i < cops.size(); ++i) itemp->addCondsp(cops[i]);
        itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, itemp) : itemp;
    }
    return makeCase(flp, selectorp, itemsp);
}

}  // namespace vhdlnative
