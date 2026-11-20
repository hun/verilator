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

bool Lowering::containsEdgeCall(const Expr& expr) const {
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        return true;
    }
    if (expr.a && containsEdgeCall(*expr.a)) return true;
    if (expr.b && containsEdgeCall(*expr.b)) return true;
    if (expr.c && containsEdgeCall(*expr.c)) return true;
    for (const ExprP& argp : expr.args) {
        if (argp && containsEdgeCall(*argp)) return true;
    }
    return false;
}

AstNodeExpr* Lowering::lowerExpr(const Expr& expr, const VType* context) {
    FileLine* const flp = expr.flp;
    switch (expr.kind) {
    case Expr::INT_LIT: return makeIntConst(flp, expr.intValue);
    case Expr::PHYS_LIT: {
        int64_t scaled = 0;
        if (!physicalValue(*m_designp, expr, scaled)) return nullptr;
        return makeTimeConst(flp, scaled);
    }
    case Expr::REAL_LIT: unsupportedError(flp, "real value in this context"); return nullptr;
    case Expr::UNAFFECTED:
        unsupportedError(flp, "'unaffected' outside an assignment");
        return nullptr;
    case Expr::TRUE_LIT: return bitConst(flp, '1');
    case Expr::FALSE_LIT: return bitConst(flp, '0');
    case Expr::CHAR_LIT: {
        // A character can itself be an enumeration literal ('z'); resolve it to the
        // enum value before treating it as a logic value or 8-bit character code.
        if (context && context->kind == VType::ENUM) {
            const std::string key = std::string{"'"} + expr.charValue + "'";
            for (size_t i = 0; i < context->literals.size(); ++i) {
                if (context->literals[i] != key) continue;
                AstConst* const constp
                    = new AstConst{flp, V3Number{flp, context->width, static_cast<uint32_t>(i)}};
                constp->dtypep(context->dtp);
                return constp;
            }
        }
        // A character in an 8-bit character context is its code; otherwise a logic value
        if (context && context->isVector() && context->width == 8) {
            return makeVectorConst(flp, *context, static_cast<unsigned char>(expr.charValue));
        }
        const char bit = vhdlLogicBit(expr.charValue);
        if (bit == 0) {
            unsupportedError(flp, std::string{"character literal '"} + expr.charValue
                                      + "' outside a logic context");
            return nullptr;
        }
        return bitConst(flp, bit);
    }
    case Expr::BITS_LIT: {
        // A string literal whose characters are all logic characters is a bit vector in a
        // logic context, but a string in a character-array context: `"ZU"` is a legal
        // std_logic_vector and a legal `string` (IEEE 1076-2008 9.3.2, 15.5)
        if (context && !expr.spelling.empty()
            && ((context->kind == VType::ARRAY && context->elemp
                 && context->elemp->typeName == "character")
                || context->typeName == "string")) {
            return makeStringConst(flp, expr.spelling);
        }
        const int width = static_cast<int>(expr.bits.size());
        AstNodeDType* dtp = nullptr;
        if (context && context->isVector() && context->width == width) {
            dtp = context->dtp;
        } else {
            dtp = findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::NOSIGN, width);
        }
        return makeBitsConst(flp, dtp, expr.bits);
    }
    case Expr::NAME: {
        if (activeEnv().count(expr.text)) return inlineBinding(flp, expr.text, false);
        // An expected enumeration type picks its own literal, even when another visible
        // enumeration declares the same name (IEEE 1076-2008 12.5)
        if (context && context->kind == VType::ENUM) {
            for (size_t i = 0; i < context->literals.size(); ++i) {
                if (VString::downcase(context->literals[i]) != VString::downcase(expr.text)) {
                    continue;
                }
                AstConst* const constp
                    = new AstConst{flp, V3Number{flp, context->width, static_cast<uint32_t>(i)}};
                constp->dtypep(context->dtp);
                return constp;
            }
        }
        if (m_ambiguousLiterals.count(expr.text)) {
            flp->v3error("VHDL: enumeration literal '" + expr.spelling + "' is ambiguous");
            return nullptr;
        }
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) return makeIntConst(flp, constIt->second);
        const auto preIt = m_preConsts.find(expr.text);
        if (preIt != m_preConsts.end()) return makeIntConst(flp, preIt->second);
        const auto valueIt = m_constValues.find(expr.text);
        if (valueIt != m_constValues.end()) return valueIt->second->cloneTree(false);
        const auto enumIt = m_enumConsts.find(expr.text);
        if (enumIt != m_enumConsts.end()) return enumIt->second->cloneTree(false);
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            if (AstNodeExpr* const aliasp = lowerAlias(expr, false)) return aliasp;
            if (m_subprograms.count(expr.text)) {
                // A function whose formals all have defaults may be called without
                // parentheses; a procedure name alone is not a value
                const std::vector<const SubprogramDecl*> visible = visibleSubprograms(expr.text);
                const bool anyFunction = std::any_of(
                    visible.begin(), visible.end(), [](const SubprogramDecl* const subp) {
                        return subp->kind == SubprogramDecl::FUNCTION;
                    });
                if (!anyFunction) {
                    flp->v3error("VHDL: procedure '" + expr.spelling + "' used in an expression");
                    return nullptr;
                }
                Expr call{Expr::CALL};
                call.flp = flp;
                call.text = expr.text;
                call.spelling = expr.spelling;
                return lowerFunctionCall(call, context);
            }
            flp->v3error("VHDL: unknown identifier '" + expr.spelling + "'");
            return nullptr;
        }
        return new AstVarRef{flp, it->second.varp, VAccess::READ};
    }
    case Expr::AGG_POSITIONAL:
    case Expr::AGG_NAMED: {
        if (!context) {
            // The aggregate's own choices say how wide it is, which is all an operand of `&`
            // needs; only an `others` association leaves the length open
            VType derived;
            if (!deriveAggregateType(expr, derived)) return nullptr;
            return lowerAggregate(expr, derived);
        }
        VType aggType = *context;
        constrainAggregateType(expr, aggType);
        return lowerAggregate(expr, aggType);
    }
    case Expr::QUAL: {
        // A type qualification fixes the operand's type and keeps its width. A
        // vector mark without a constraint takes an aggregate operand's bounds
        // from the choices it covers.
        const auto declared = m_types.find(expr.text);
        const bool builtinVector = expr.text == "std_logic_vector"
                                   || expr.text == "std_ulogic_vector" || expr.text == "bit_vector"
                                   || expr.text == "unsigned" || expr.text == "signed";
        const bool aggregateOperand
            = expr.a->kind == Expr::AGG_POSITIONAL || expr.a->kind == Expr::AGG_NAMED;
        AstNodeExpr* valuep = nullptr;
        if (aggregateOperand && declared != m_types.end()) {
            valuep = lowerExpr(*expr.a, &declared->second);
        } else if (aggregateOperand && builtinVector) {
            VType aggregateType;
            aggregateType.kind = VType::VECTOR;
            aggregateType.kwd
                = expr.text == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
            aggregateType.signing = vectorSigning(expr.text);
            int64_t low = 0;
            int64_t high = 0;
            if (!aggregateBounds(*expr.a, low, high)) {
                unsupportedError(flp, "qualified aggregate with a non-static choice");
                return nullptr;
            }
            aggregateType.left = static_cast<int>(low);
            aggregateType.right = static_cast<int>(high);
            aggregateType.ascending = true;
            aggregateType.width = static_cast<int>(high - low + 1);
            aggregateType.dtp
                = findVector(flp, aggregateType.kwd, aggregateType.signing, aggregateType.left,
                             aggregateType.right, aggregateType.ascending);
            valuep = lowerExpr(*expr.a, &aggregateType);
        } else {
            valuep = lowerExpr(*expr.a, context);
        }
        if (!valuep) return nullptr;
        const VType valueType = typeOfDType(valuep->dtypep());
        if (declared != m_types.end()) return resizeExpr(valuep, declared->second, flp);
        // A predefined scalar mark keeps its scalar type (IEEE 1076-2008 9.3.6),
        // so `integer'(x)` stays an integer instead of becoming a vector
        TypeSpec markSpec;
        markSpec.flp = flp;
        markSpec.mark = expr.text;
        VType scalar;
        if (formalType(markSpec, scalar) && !scalar.isVector()) {
            return resizeExpr(valuep, scalar, flp);
        }
        VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
        VSigning signing = VSigning::NOSIGN;
        if (expr.text == "bit_vector" || expr.text == "bit") kwd = VBasicDTypeKwd::BIT;
        if (expr.text == "signed" || expr.text == "unsigned") signing = vectorSigning(expr.text);
        const VType target = vectorType(flp, kwd, signing, valueType.width);
        return resizeExpr(valuep, target, flp);
    }
    case Expr::ATTR: return lowerAttr(expr);
    case Expr::SELECTED: return lowerSelectedExpr(expr);
    case Expr::PAREN: return lowerExpr(*expr.a, context);
    case Expr::UNARY: {
        bool userFound = false;
        if (AstNodeExpr* const userp = lowerUserUnary(expr, context, userFound)) return userp;
        if (userFound) return nullptr;
        AstNodeExpr* operandp = lowerExpr(*expr.a, nullptr);
        if (!operandp) return nullptr;
        const VType type = typeOfDType(operandp->dtypep());
        const bool reduction = expr.text == "and" || expr.text == "or" || expr.text == "xor"
                               || expr.text == "nand" || expr.text == "nor" || expr.text == "xnor";
        if (reduction) return makeReduction(flp, operandp, expr.text);
        if (expr.text == "abs") {
            // numeric_std defines "abs" for a signed operand (IEEE 1076-2008
            // Annex C), which compares and negates in place
            const bool signedVec = type.isVector() && type.isSigned();
            if (!type.isInt() && !signedVec) {
                unsupportedError(flp, "abs of a non-integer value");
                operandp->deleteTree();
                return nullptr;
            }
            // |x| is x when x is not negative, and -x otherwise
            AstNodeExpr* const zerop = type.isInt()
                                           ? static_cast<AstNodeExpr*>(makeIntConst(flp, 0))
                                           : makeVectorConst(flp, type, 0);
            AstNodeExpr* const ltZeroP = new AstLtS{flp, operandp->cloneTree(false), zerop};
            ltZeroP->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            AstNegate* const negp = new AstNegate{flp, operandp->cloneTree(false)};
            negp->dtypep(operandp->dtypep());
            AstCond* const resultp = new AstCond{flp, ltZeroP, negp, operandp};
            resultp->dtypep(operandp->dtypep());
            return resultp;
        }
        if (expr.text == "??") {
            // Implicit condition: false for metavalues, which the 2-state model has none of
            AstNodeExpr* const oneConstp = operandp->dtypep()->width() == 1
                                               ? bitConst(flp, '1')
                                               : resizeExpr(bitConst(flp, '1'), type, flp);
            AstEqCase* const eqp = new AstEqCase{flp, operandp, oneConstp};
            eqp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            return eqp;
        }
        if (expr.text == "not") {
            VType operandType;
            if (!actualType(*expr.a, operandType)) operandType = type;
            // `not` is predefined for bit, boolean and arrays of them; an integer,
            // enumeration or character array has no such operator (IEEE 1076-2008 9.2.2)
            const bool logicArray = operandType.kind != VType::ARRAY || !operandType.elemp
                                    || (operandType.elemWidth == 1
                                        && (operandType.elemp->typeName.empty()
                                            || operandType.elemp->typeName == "std_ulogic"
                                            || operandType.elemp->typeName == "bit"
                                            || operandType.elemp->typeName == "boolean"));
            if (operandType.kind == VType::INT || operandType.kind == VType::REAL
                || operandType.kind == VType::ENUM || operandType.kind == VType::FILE
                || operandType.kind == VType::LINE || !logicArray) {
                flp->v3error("VHDL: 'not' operand is not a logic or bit value");
                operandp->deleteTree();
                return nullptr;
            }
            AstNot* const notp = new AstNot{flp, operandp};
            notp->dtypep(type.dtp);
            return notp;
        }
        if (expr.text == "+") return operandp;
        if (expr.text == "-") {
            // numeric_std defines unary minus for signed vectors and integers
            if (!type.isInt() && !(type.isVector() && type.isSigned())) {
                unsupportedError(flp, "unary '-' on a non-signed value");
                operandp->deleteTree();
                return nullptr;
            }
            AstNegate* const negp = new AstNegate{flp, operandp};
            negp->dtypep(type.dtp);
            return negp;
        }
        unsupportedError(flp, "unary operator '" + expr.text + "'");
        operandp->deleteTree();
        return nullptr;
    }
    case Expr::STRING_LIT: {
        // A string literal is an array of character; pack it as an unsigned vector
        // of 8-bit codes, first character most significant, matching a string
        // generic so assignments and comparisons line up without a known target.
        if (expr.spelling.empty()) {
            unsupportedError(flp, "empty string literal");
            return nullptr;
        }
        return makeStringConst(flp, expr.spelling);
    }
    case Expr::BINARY: return lowerBinary(expr, context);
    case Expr::INDEX: {
        // An expression that converts a real back to an integer folds whole at
        // elaboration time: the real itself has no form in the model, but the integer
        // result does. math_pkg's `floor_log2` is `natural(log2(real(value)))`
        if (expr.a && expr.a->kind == Expr::NAME
            && (expr.a->text == "natural" || expr.a->text == "positive"
                || expr.a->text == "integer")
            && mentionsRealConversion(expr)) {
            int64_t foldedValue = 0;
            if (evalInt(expr, foldedValue)) return makeIntConst(flp, foldedValue);
        }
        // A qualified call `package.name(...)`: a selected callee whose prefix names a
        // package, rather than an element of a record field
        std::string packageName;
        if (expr.a && expr.a->kind == Expr::SELECTED && expr.a->a
            && packageNameOf(*expr.a->a, packageName)) {
            return lowerFunctionCall(expr, context);
        }
        if (expr.a && expr.a->kind == Expr::NAME) {
            if (expr.a->text == "rising_edge" || expr.a->text == "falling_edge") {
                return lowerEdgeCall(expr);
            }
            // A user subprogram shadows a standard-package function of the same name
            if (m_subprograms.count(expr.a->text)) return lowerFunctionCall(expr, context);
            if (isIntrinsic(expr.a->text)) return lowerIntrinsic(expr);
            if (isConversion(expr.a->text)) return lowerConversion(expr, context);
            // A real has no synthesizable form, so a conversion to one is refused by name
            if (expr.a->text == "real") {
                unsupportedError(flp, "conversion to a real value");
                return nullptr;
            }
            // A predefined scalar type mark applied to a value is the identity conversion
            if (expr.a->text == "boolean" || expr.a->text == "bit" || expr.a->text == "std_logic"
                || expr.a->text == "std_ulogic") {
                if (expr.args.size() != 1) {
                    unsupportedError(flp, "type conversion with "
                                              + std::to_string(expr.args.size()) + " arguments");
                    return nullptr;
                }
                return lowerExpr(*expr.args[0], nullptr);
            }
            // A declared type mark applied to a value is a type conversion
            const auto typeIt = m_types.find(expr.a->text);
            if (typeIt != m_types.end()) {
                if (expr.args.size() != 1) {
                    unsupportedError(flp, "type conversion with "
                                              + std::to_string(expr.args.size()) + " arguments");
                    return nullptr;
                }
                const VType& target = typeIt->second;
                AstNodeExpr* valuep = lowerExpr(*expr.args[0], target.dtp ? &target : nullptr);
                if (!valuep) return nullptr;
                if (!target.dtp) {
                    // A conversion to an unconstrained array takes its bounds from the
                    // operand. Closely related arrays have the same packed form, so the
                    // operand is the result; anything else has no representable size.
                    const VType valueType = typeOfDType(valuep->dtypep());
                    const VType targetElem = elementType(target, flp);
                    const VType valueElem = elementType(valueType, flp);
                    const bool sameType
                        = !target.typeName.empty() && target.typeName == valueType.typeName;
                    const bool sameElement = target.isVector() && valueType.isVector()
                                             && target.elemWidth == valueType.elemWidth
                                             && targetElem.kind == valueElem.kind;
                    if (sameType || sameElement) return valuep;
                    unsupportedError(flp,
                                     "conversion to unconstrained type '" + expr.a->text + "'");
                    valuep->deleteTree();
                    return nullptr;
                }
                return resizeExpr(valuep, target, flp);
            }
        }
        return lowerDesignator(expr, false);
    }
    case Expr::CALL: {
        // An operator-symbol call to a unary reduction, e.g. `"or"(a)`
        const std::string& name = expr.text;
        const bool reduction = name == "and" || name == "or" || name == "xor" || name == "nand"
                               || name == "nor" || name == "xnor";
        if (reduction && expr.args.size() == 1 && !m_subprograms.count(name)) {
            AstNodeExpr* const operandp = lowerExpr(*expr.args[0], nullptr);
            if (!operandp) return nullptr;
            return makeReduction(flp, operandp, name);
        }
        return lowerFunctionCall(expr, context);
    }
    case Expr::SLICE: return lowerDesignator(expr, false);
    case Expr::AGG_OTHERS: {
        if (!context) {
            unsupportedError(flp, "aggregate without a known target type");
            return nullptr;
        }
        char bit = 0;
        if (expr.a->kind == Expr::CHAR_LIT) {
            bit = vhdlLogicBit(expr.a->charValue);
        } else if (expr.a->kind == Expr::BITS_LIT && expr.a->bits.size() == 1) {
            bit = expr.a->bits[0];
        }
        if (bit != 0) return makeRepeatedConst(flp, *context, bit);
        // An arbitrary value is replicated across every element
        if (!context->isVector()) {
            unsupportedError(flp, "aggregate value for a non-array type");
            return nullptr;
        }
        VType elemType = elementType(*context, flp);
        AstNodeExpr* valuep = lowerExpr(*expr.a, &elemType);
        if (!valuep) return nullptr;
        valuep = resizeExpr(valuep, elemType, flp);
        return repeatValue(flp, valuep, context->length(), context->dtp);
    }
    }
    return nullptr;
}

//! Lexicographic ordering of two VHDL arrays. Arrays of signed integers flip each
//! element's sign bit, which turns signed element order into unsigned packed order.
AstNodeExpr* Lowering::lowerArrayRelation(FileLine* flp, const std::string& cmpOp, const VType& lt,
                                          const VType& rt, AstNodeExpr*& lp, AstNodeExpr*& rp,
                                          AstNodeDType* bitDtp) {
    const VType* structp = nullptr;
    if (lt.kind == VType::ARRAY && lt.elemp)
        structp = &lt;
    else if (rt.kind == VType::ARRAY && rt.elemp)
        structp = &rt;
    int elemWidth = 1;
    bool elemSigned = false;
    VType elem;
    if (structp) {
        if (lt.kind != VType::ARRAY || rt.kind != VType::ARRAY || !lt.elemp || !rt.elemp) {
            flp->v3error("VHDL: comparison of unrelated array types");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        elemWidth = structp->elemWidth;
        elem = *structp->elemp;
        elemSigned = elem.kind == VType::INT;
        if (elem.kind != VType::INT && elem.kind != VType::ENUM && elem.kind != VType::BOOLEAN
            && elem.kind != VType::LOGIC && elem.kind != VType::BIT) {
            flp->v3error("VHDL: array ordering requires a discrete element type");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
    } else {
        elem = elementType(lt, flp);
        if (elem.kind != VType::LOGIC && elem.kind != VType::BIT) {
            flp->v3error("VHDL: array ordering requires a discrete element type");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
    }
    const int lLen = lt.kind == VType::ARRAY ? lt.length() : lt.width;
    const int rLen = rt.kind == VType::ARRAY ? rt.length() : rt.width;
    // A null array has no elements; only its length orders it
    if (lLen <= 0 || rLen <= 0) {
        lp->deleteTree();
        rp->deleteTree();
        bool result = false;
        if (cmpOp == "<" || cmpOp == "<=") {
            result = lLen < rLen || (cmpOp == "<=" && lLen == rLen);
        } else {
            result = lLen > rLen || (cmpOp == ">=" && lLen == rLen);
        }
        return bitConst(flp, result ? '1' : '0');
    }
    if (lLen * elemWidth != lt.width || rLen * elemWidth != rt.width) {
        flp->v3error("VHDL: comparison of arrays with different element widths");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    const auto flipSigns = [&](AstNodeExpr* nodep, int elements) -> AstNodeExpr* {
        V3Number maskNum{flp, nodep->width(), 0u};
        for (int i = 0; i < elements; ++i) { maskNum.setBit(i * elemWidth + elemWidth - 1, '1'); }
        AstConst* const maskp = new AstConst{flp, maskNum};
        maskp->dtypep(findVectorWidth(flp, lt.kwd, VSigning::NOSIGN, nodep->width()));
        AstXor* const xorp = new AstXor{flp, nodep, maskp};
        xorp->dtypep(findVectorWidth(flp, lt.kwd, VSigning::NOSIGN, nodep->width()));
        return xorp;
    };
    if (elemSigned) {
        lp = flipSigns(lp, lLen);
        rp = flipSigns(rp, rLen);
    }
    if (lLen == rLen) {
        AstNodeExpr* cmpp = nullptr;
        if (cmpOp == "<") {
            cmpp = new AstLt{flp, lp, rp};
        } else if (cmpOp == "<=") {
            cmpp = new AstLte{flp, lp, rp};
        } else if (cmpOp == ">") {
            cmpp = new AstGt{flp, lp, rp};
        } else {
            cmpp = new AstGte{flp, lp, rp};
        }
        cmpp->dtypep(bitDtp);
        return cmpp;
    }
    const int commonElems = std::min(lLen, rLen);
    const int commonBits = commonElems * elemWidth;
    const VType prefixType = vectorType(flp, lt.kwd, VSigning::NOSIGN, commonBits);
    AstSel* const lpfx = new AstSel{flp, lp->cloneTree(false), lt.width - commonBits, commonBits};
    lpfx->dtypep(prefixType.dtp);
    AstSel* const rpfx = new AstSel{flp, rp->cloneTree(false), rt.width - commonBits, commonBits};
    rpfx->dtypep(prefixType.dtp);
    lp->deleteTree();
    rp->deleteTree();
    lp = nullptr;
    rp = nullptr;
    AstNodeExpr* const eqp = new AstEqCase{flp, lpfx->cloneTree(false), rpfx->cloneTree(false)};
    eqp->dtypep(bitDtp);
    AstNodeExpr* prefixp = nullptr;
    if (cmpOp == "<" || cmpOp == "<=") {
        prefixp = new AstLt{flp, lpfx, rpfx};
    } else {
        prefixp = new AstGt{flp, lpfx, rpfx};
    }
    prefixp->dtypep(bitDtp);
    // The prefix is equal and the lengths differ; the shorter array is the smaller one
    const bool lengthWins = (cmpOp == "<" || cmpOp == "<=") ? lLen < rLen : lLen > rLen;
    AstNodeExpr* const lengthp = bitConst(flp, lengthWins ? '1' : '0');
    AstAnd* const andp = new AstAnd{flp, eqp, lengthp};
    andp->dtypep(bitDtp);
    AstOr* const resultp = new AstOr{flp, prefixp, andp};
    resultp->dtypep(bitDtp);
    return resultp;
}

AstNodeExpr* Lowering::lowerBinary(const Expr& expr, const VType* context) {
    FileLine* const flp = expr.flp;
    const std::string& op = expr.text;
    // A static integer expression must fit the 32-bit integer type (IEEE 1076-2008 9.1);
    // the unrolled multiply or the middle end would otherwise wrap it silently
    if ((op == "+" || op == "-" || op == "*" || op == "/" || op == "mod" || op == "rem"
         || op == "**")
        && expr.a && expr.b && literalIntegerExpr(expr)) {
        VType aType;
        VType bType;
        if (actualType(*expr.a, aType) && actualType(*expr.b, bType) && aType.isInt()
            && bType.isInt() && aType.width <= 32 && bType.width <= 32) {
            int64_t value = 0;
            if (evalInt(expr, value) && (value < -0x80000000LL || value > 0x7fffffffLL)) {
                flp->v3error("VHDL: integer overflow in a static expression");
                return nullptr;
            }
        }
    }
    // An explicitly declared operator with matching parameter types takes precedence over
    // the built-in operator of the same symbol
    if (expr.a && expr.b) {
        bool userFound = false;
        if (AstNodeExpr* const userp = lowerUserOperator(expr, context, userFound)) {
            return userp;
        }
        if (userFound) return nullptr;
    }
    // A relational operator whose static operands are reals folds to a bit before
    // either operand is lowered, so a real generic/constant compared to a literal (as in
    // a `when` condition, a generate, or `integer(x) = 8`) becomes a constant. It fires
    // only when at least one operand transitively mentions a real, so ordinary integer
    // or signal comparisons keep their existing path.
    if (expr.a && expr.b) {
        const std::string& rop = op[0] == '?' ? op.substr(1) : op;
        if ((rop == "=" || rop == "/=" || rop == "<" || rop == "<=" || rop == ">" || rop == ">=")
            && (mentionsReal(*expr.a) || mentionsReal(*expr.b))) {
            double lhs = 0.0;
            double rhs = 0.0;
            if (evalReal(*expr.a, lhs) && evalReal(*expr.b, rhs)) {
                bool result = false;
                if (rop == "=") {
                    result = lhs == rhs;
                } else if (rop == "/=") {
                    result = lhs != rhs;
                } else if (rop == "<") {
                    result = lhs < rhs;
                } else if (rop == "<=") {
                    result = lhs <= rhs;
                } else if (rop == ">") {
                    result = lhs > rhs;
                } else {
                    result = lhs >= rhs;
                }
                AstConst* const bitp
                    = new AstConst{flp, V3Number{flp, 1, static_cast<uint32_t>(result)}};
                bitp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
                return bitp;
            }
        }
    }
    // Lower the side that is not a bare character literal first; a bare character
    // literal in the other slot then adopts that operand's type, so `inp >= 'a'`
    // compares character codes instead of mistaking `'a'` for a logic value.
    AstNodeExpr* lp = nullptr;
    AstNodeExpr* rp = nullptr;
    const bool aChar = expr.a && expr.a->kind == Expr::CHAR_LIT;
    const bool bChar = expr.b && expr.b->kind == Expr::CHAR_LIT;
    const bool aEnumLit = expr.a && expr.a->kind == Expr::NAME && m_enumConsts.count(expr.a->text);
    const bool bEnumLit = expr.b && expr.b->kind == Expr::NAME && m_enumConsts.count(expr.b->text);
    // A logical operator combines a scalar character literal element-wise with the
    // other operand (IEEE 1076-2008 9.2.2), so the literal stays a logic value and
    // never takes the character-code path an 8-bit operand context would give it.
    const bool logicalOp
        = op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor" || op == "xnor";
    // A character literal is one character of a concatenated character array, but one
    // logic element of a concatenated logic or bit vector, even an eight-bit one
    const auto characterArray = [&](const Expr& other) {
        if (other.kind == Expr::STRING_LIT || other.kind == Expr::CHAR_LIT) return true;
        VType otherType;
        if (!actualType(other, otherType)) return false;
        // A character is an element of a character array, so concatenating one with a
        // character literal is a string rather than two logic elements
        return otherType.kind == VType::ARRAY || otherType.typeName == "character";
    };
    if (bChar && !aChar) {
        lp = lowerExpr(*expr.a, nullptr);
        if (!lp) return nullptr;
        const VType lctx = typeOfDType(lp->dtypep());
        const bool charCode = op != "&" || characterArray(*expr.a);
        rp = lowerExpr(*expr.b, logicalOp || !charCode ? nullptr : &lctx);
    } else if (aChar && !bChar) {
        rp = lowerExpr(*expr.b, nullptr);
        if (!rp) return nullptr;
        const VType rctx = typeOfDType(rp->dtypep());
        const bool charCode = op != "&" || characterArray(*expr.b);
        lp = lowerExpr(*expr.a, logicalOp || !charCode ? nullptr : &rctx);
    } else if (aEnumLit != bEnumLit) {
        // Resolve a bare enumeration literal from the type of the other operand, which is
        // how two visible enumeration types can share a literal name
        const Expr& typed = aEnumLit ? *expr.b : *expr.a;
        AstNodeExpr* typedp = lowerExpr(typed, nullptr);
        if (!typedp) return nullptr;
        VType enumType;
        bool haveEnum = typeOfDesignator(typed, enumType) && enumType.kind == VType::ENUM;
        if (!haveEnum) haveEnum = enumTypeOfDType(typedp->dtypep(), enumType);
        if (haveEnum) {
            AstNodeExpr* const litp = lowerExpr(aEnumLit ? *expr.a : *expr.b, &enumType);
            if (!litp) {
                typedp->deleteTree();
                return nullptr;
            }
            if (aEnumLit) {
                lp = litp;
                rp = typedp;
            } else {
                lp = typedp;
                rp = litp;
            }
        } else {
            typedp->deleteTree();
            lp = lowerExpr(*expr.a, nullptr);
            if (!lp) return nullptr;
            rp = lowerExpr(*expr.b, nullptr);
        }
    } else {
        // A typed operand fixes the type of a call or aggregate beside it, e.g.
        // `C <= C + ite(c, (C'range => '1'), x)` takes the unsigned ite (IEEE 1076-2008
        // 9.1). Concatenation and shifts combine operands of different types, so they
        // give no such context.
        const bool sameType = op == "+" || op == "-" || op == "*" || op == "/" || op == "="
                              || op == "/=" || op == "<" || op == "<=" || op == ">" || op == ">="
                              || logicalOp;
        VType aType;
        VType bType;
        const bool aKnown = sameType && actualType(*expr.a, aType);
        const bool bKnown = sameType && actualType(*expr.b, bType);
        if (aKnown != bKnown) {
            const bool lowerLeftFirst = aKnown;
            AstNodeExpr* const knownp = lowerExpr(lowerLeftFirst ? *expr.a : *expr.b, nullptr);
            if (!knownp) return nullptr;
            const VType known = typeOfLowered(lowerLeftFirst ? *expr.a : *expr.b, knownp);
            // numeric_std's arithmetic operators take unconstrained array formals, so an
            // aggregate operand keeps the bounds its own choices give it
            // (IEEE 1076-2008 9.3.3.3): `ac + bc + (0 to 0 => c)` adds a one-element vector
            const Expr& untyped = lowerLeftFirst ? *expr.b : *expr.a;
            const bool arithmeticOp
                = op == "+" || op == "-" || op == "*" || op == "/" || op == "mod" || op == "rem";
            const bool selfTypedAggregate
                = arithmeticOp
                  && (untyped.kind == Expr::AGG_POSITIONAL || untyped.kind == Expr::AGG_NAMED)
                  && !aggregateHasOthers(untyped);
            const VType* const ctxp
                = !selfTypedAggregate && (known.isVector() || known.kind == VType::ARRAY)
                      ? &known
                      : nullptr;
            if (lowerLeftFirst) {
                lp = knownp;
                rp = lowerExpr(*expr.b, ctxp);
            } else {
                rp = knownp;
                lp = lowerExpr(*expr.a, ctxp);
            }
        } else {
            lp = lowerExpr(*expr.a, nullptr);
            if (!lp) return nullptr;
            rp = lowerExpr(*expr.b, nullptr);
        }
    }
    if (!rp) {
        if (lp) lp->deleteTree();
        return nullptr;
    }
    if (!lp) return nullptr;
    VType lt = typeOfLowered(*expr.a, lp);
    VType rt = typeOfLowered(*expr.b, rp);
    if (logicalOp && !logicalOperandsConform(*expr.a, *expr.b, lt, rt)) {
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    const bool relationalOp = op == "=" || op == "/=" || op == "<" || op == "<=" || op == ">"
                              || op == ">=" || op == "?=" || op == "?/=" || op == "?<"
                              || op == "?<=" || op == "?>" || op == "?>=";
    if (relationalOp && !relationalOperandsConform(*expr.a, *expr.b)) {
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    AstNodeDType* const bitDtp = findBasic(flp, VBasicDTypeKwd::LOGIC);

    if (op == "?=" || op == "?/=") {
        if (lt.isInt() != rt.isInt()) {
            // As with the plain relational operator, an integer matches a vector of the
            // same value by taking on the vector's type.
            const bool lhsInt = lt.isInt();
            const VType& vectorT = lhsInt ? rt : lt;
            AstNodeExpr*& intOperand = lhsInt ? lp : rp;
            int64_t value = 0;
            if (evalInt(lhsInt ? *expr.a : *expr.b, value)) {
                intOperand->deleteTree();
                intOperand = makeVectorConst(flp, vectorT, value);
            } else {
                intOperand = resizeExpr(intOperand, vectorT, flp);
            }
            (lhsInt ? lt : rt) = vectorT;
        }
        if (lt.width != rt.width) rp = resizeExpr(rp, lt, flp);
        AstNodeExpr* matchp = lowerMatching(expr, lp, rp);
        if (!matchp) return nullptr;
        if (op == "?/=") {
            AstNot* const notp = new AstNot{flp, matchp};
            notp->dtypep(matchp->dtypep());
            return notp;
        }
        return matchp;
    }
    // A matching relational operator compares like the plain operator, but a '-' in an
    // operand would need don't-care semantics that the 2-state model cannot express
    if (op == "?<" || op == "?<=" || op == "?>" || op == "?>=") {
        for (const Expr* const sidep : {expr.a.get(), expr.b.get()}) {
            if (!sidep || sidep->kind != Expr::BITS_LIT) continue;
            if (sidep->bits.find('-') != std::string::npos) {
                unsupportedError(flp, "matching relational operator with a don't-care operand");
                lp->deleteTree();
                rp->deleteTree();
                return nullptr;
            }
        }
    }
    const std::string cmpOp = op[0] == '?' ? op.substr(1) : op;

    const bool relational = cmpOp == "=" || cmpOp == "/=" || cmpOp == "<" || cmpOp == "<="
                            || cmpOp == ">" || cmpOp == ">=";
    if (relational) {
        if (lt.isInt() && rt.isInt()) {
            AstNodeExpr* ip = nullptr;
            if (cmpOp == "=") {
                ip = new AstEq{flp, lp, rp};
            } else if (cmpOp == "/=") {
                ip = new AstNeq{flp, lp, rp};
            } else if (cmpOp == "<") {
                ip = new AstLtS{flp, lp, rp};
            } else if (cmpOp == "<=") {
                ip = new AstLteS{flp, lp, rp};
            } else if (cmpOp == ">") {
                ip = new AstGtS{flp, lp, rp};
            } else {
                ip = new AstGteS{flp, lp, rp};
            }
            // Tag the result like every other comparison so an integer test mixed with a
            // vector test in a logical expression sees matching operand types
            ip->dtypep(bitDtp);
            return ip;
        }
        if (lt.isInt() != rt.isInt()) {
            // numeric_std/std_logic_arith compare a vector with an integer of the same
            // value by converting the integer to the vector's type: fold a static integer
            // to a constant, or resize a dynamic one exactly as the arithmetic path does
            const bool lhsInt = lt.isInt();
            const VType& vectorT = lhsInt ? rt : lt;
            // std_logic_arith compares an unsigned operand as a signed value one bit
            // wider, so a negative integer is less than every unsigned value instead of
            // matching its two's-complement bit pattern
            if (m_stdLogicArith && vectorT.isVector() && vectorT.signing == VSigning::UNSIGNED) {
                const VType arithType
                    = vectorType(flp, vectorT.kwd, VSigning::SIGNED, vectorT.width + 1);
                lp = resizeExpr(lp, arithType, flp);
                rp = resizeExpr(rp, arithType, flp);
                lt = arithType;
                rt = arithType;
            } else {
                AstNodeExpr*& intOperand = lhsInt ? lp : rp;
                int64_t value = 0;
                if (evalInt(lhsInt ? *expr.a : *expr.b, value)) {
                    intOperand->deleteTree();
                    intOperand = makeVectorConst(flp, vectorT, value);
                } else {
                    intOperand = resizeExpr(intOperand, vectorT, flp);
                }
                (lhsInt ? lt : rt) = vectorT;
            }
        } else if (m_stdLogicArith && lt.isVector() && rt.isVector()
                   && lt.signing != VSigning::NOSIGN && rt.signing != VSigning::NOSIGN
                   && lt.isSigned() != rt.isSigned()) {
            // std_logic_arith's mixed comparison widens the unsigned operand by one bit
            // before comparing as signed, so the unsigned value never aliases a negative
            // signed value
            const int unsignedWidth = lt.isSigned() ? rt.width : lt.width;
            const int signedWidth = lt.isSigned() ? lt.width : rt.width;
            const VType arithType = vectorType(flp, lt.kwd, VSigning::SIGNED,
                                               std::max(unsignedWidth + 1, signedWidth));
            lp = resizeExpr(lp, arithType, flp);
            rp = resizeExpr(rp, arithType, flp);
            lt = arithType;
            rt = arithType;
        }
        // A bare bit-string literal is typeless: a comparison like `-vs >= "110"`
        // resolves it to the signedness of the real operand so a signed/unsigned
        // mismatch between the literal and that operand does not arise.
        auto reSignLiteral = [&](const Expr& side, AstNodeExpr*& operandp, VType& opType,
                                 const VType& modelType) {
            if (side.kind != Expr::BITS_LIT || !opType.isVector() || !modelType.isVector()) return;
            if (opType.isSigned() == modelType.isSigned()) return;
            operandp->deleteTree();
            const VType fixedType
                = vectorType(flp, modelType.kwd, modelType.signing, opType.width);
            operandp = makeBitsConst(flp, fixedType.dtp, side.bits);
            opType = fixedType;
        };
        reSignLiteral(*expr.a, lp, lt, rt);
        reSignLiteral(*expr.b, rp, rt, lt);
        if (m_stdLogicSigned
            && ((lt.kind == VType::VECTOR && lt.kwd == VBasicDTypeKwd::LOGIC)
                || (rt.kind == VType::VECTOR && rt.kwd == VBasicDTypeKwd::LOGIC))) {
            unsupportedError(flp, "std_logic_signed comparison");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        // VHDL array equality: different lengths are unequal, equal lengths compare
        // element by element. A packed bitwise identity comparison is the same test.
        if ((cmpOp == "=" || cmpOp == "/=") && lt.isVhdlArray() && rt.isVhdlArray()) {
            if (lt.width != rt.width) {
                lp->deleteTree();
                rp->deleteTree();
                AstConst* const resultp
                    = new AstConst{flp, V3Number{flp, 1, static_cast<uint32_t>(cmpOp == "/=")}};
                resultp->dtypep(bitDtp);
                return resultp;
            }
            if (lt.width == 0) {
                lp->deleteTree();
                rp->deleteTree();
                AstConst* const resultp
                    = new AstConst{flp, V3Number{flp, 1, static_cast<uint32_t>(cmpOp == "/=")}};
                resultp->dtypep(bitDtp);
                return resultp;
            }
            AstNodeExpr* cmpp = cmpOp == "="
                                    ? static_cast<AstNodeExpr*>(new AstEqCase{flp, lp, rp})
                                    : static_cast<AstNodeExpr*>(new AstNeqCase{flp, lp, rp});
            cmpp->dtypep(bitDtp);
            return cmpp;
        }
        // IEEE 1164 defines `=` on std_logic as identity, so 'X' = 'X' is true; a 4-state
        // equality would report false. The same applies element-wise to a logic vector.
        const bool logicCompare
            = (lt.kind == VType::LOGIC && rt.kind == VType::LOGIC)
              || (lt.kind == VType::VECTOR && rt.kind == VType::VECTOR
                  && lt.kwd == VBasicDTypeKwd::LOGIC && rt.kwd == VBasicDTypeKwd::LOGIC);
        if ((cmpOp == "=" || cmpOp == "/=") && logicCompare && lt.width == rt.width) {
            AstNodeExpr* cmpp = nullptr;
            if (cmpOp == "=") {
                cmpp = new AstEqCase{flp, lp, rp};
            } else {
                cmpp = new AstNeqCase{flp, lp, rp};
            }
            cmpp->dtypep(bitDtp);
            return cmpp;
        }
        // VHDL array ordering is lexicographic over the elements; a shorter array is
        // smaller when the common prefix is equal (IEEE 1076-2008 9.2.3).
        if ((cmpOp == "<" || cmpOp == "<=" || cmpOp == ">" || cmpOp == ">=") && lt.isVhdlArray()
            && rt.isVhdlArray()) {
            return lowerArrayRelation(flp, cmpOp, lt, rt, lp, rp, bitDtp);
        }
        if (lt.isVector() && lt.isSigned() != rt.isSigned()) {
            unsupportedError(flp, "comparison between signed and unsigned operands");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (lt.isVector()) {
            const int width = std::max(lt.width, rt.width);
            const VType widenType = vectorType(flp, lt.kwd, lt.signing, width);
            lp = resizeExpr(lp, widenType, flp);
            rp = resizeExpr(rp, widenType, flp);
        }
        AstNodeExpr* cmpp = nullptr;
        const bool sign = lt.isSigned();
        if (cmpOp == "=") {
            cmpp = new AstEq{flp, lp, rp};
        } else if (cmpOp == "/=") {
            cmpp = new AstNeq{flp, lp, rp};
        } else if (cmpOp == "<") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstLtS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstLt{flp, lp, rp});
        } else if (cmpOp == "<=") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstLteS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstLte{flp, lp, rp});
        } else if (cmpOp == ">") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstGtS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstGt{flp, lp, rp});
        } else {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstGteS{flp, lp, rp})
                        : static_cast<AstNodeExpr*>(new AstGte{flp, lp, rp});
        }
        cmpp->dtypep(bitDtp);
        return cmpp;
    }

    if (op == "&") {
        if (!lt.isVector() && !rt.isVector()) {
            if (lt.kind != rt.kind) {
                unsupportedError(flp, "concatenation of mismatched element types");
                lp->deleteTree();
                rp->deleteTree();
                return nullptr;
            }
        } else if (lt.isVector() && rt.isVector() && lt.kwd != rt.kwd) {
            unsupportedError(flp, "concatenation of mismatched element types");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        } else if (lt.isVector() && rt.isVector() && !lt.typeName.empty() && !rt.typeName.empty()
                   && (expr.a->kind == Expr::NAME || expr.a->kind == Expr::SELECTED
                       || expr.a->kind == Expr::INDEX || expr.a->kind == Expr::SLICE)
                   && (expr.b->kind == Expr::NAME || expr.b->kind == Expr::SELECTED
                       || expr.b->kind == Expr::INDEX || expr.b->kind == Expr::SLICE)
                   && !arrayBaseTypesRelated(lt.typeName, rt.typeName)) {
            flp->v3error("VHDL: concatenation of operands of unrelated array types");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (lt.isInt() || rt.isInt()) {
            unsupportedError(flp, "concatenation with an integer operand");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        // A bit-string literal beside a numeric_std vector takes that vector's type, so
        // `"0" & unsigned(x)` stays unsigned and can be added (IEEE 1076-2008 9.3.6)
        const VType& named = !lt.isVector()                   ? rt
                             : !rt.isVector()                 ? lt
                             : lt.signing == VSigning::NOSIGN ? rt
                                                              : lt;
        const VType resultType = vectorType(flp, named.kwd, named.signing, lt.width + rt.width);
        AstConcat* const concatp = new AstConcat{flp, lp, rp};
        concatp->dtypep(resultType.dtp);
        return concatp;
    }

    const bool logical
        = op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor" || op == "xnor";
    if (logical) {
        // IEEE 1164 and the predefined operators define and/or/xor/... between a vector and
        // a single scalar bit, applying the scalar to every element: replicate it to width.
        if (lt.isVector() != rt.isVector()) {
            const bool lhsVector = lt.isVector();
            AstNodeExpr*& scalarp = lhsVector ? rp : lp;
            VType& scalarType = lhsVector ? rt : lt;
            const VType& vecT = lhsVector ? lt : rt;
            const bool scalarBit = scalarType.kind == VType::BIT;
            const bool vectorBit = vecT.kwd == VBasicDTypeKwd::BIT;
            if (scalarType.width == 1 && scalarBit == vectorBit) {
                AstNodeExpr* const replp
                    = new AstReplicate{flp, scalarp, static_cast<uint32_t>(vecT.width)};
                replp->dtypep(vecT.dtp);
                scalarp = replp;
                scalarType = vecT;
            }
        }
        if (lt.width != rt.width || lt.kind != rt.kind) {
            unsupportedError(flp, "logical operation on operands of different types");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        AstNodeExpr* resultp = nullptr;
        if (op == "and" || op == "nand") {
            resultp = new AstAnd{flp, lp, rp};
        } else if (op == "or" || op == "nor") {
            resultp = new AstOr{flp, lp, rp};
        } else {
            resultp = new AstXor{flp, lp, rp};
        }
        resultp->dtypep(lt.dtp);
        if (op == "nand" || op == "nor" || op == "xnor") {
            AstNot* const notp = new AstNot{flp, resultp};
            notp->dtypep(lt.dtp);
            return notp;
        }
        return resultp;
    }

    if (op == "sll" || op == "srl" || op == "sla" || op == "sra" || op == "rol" || op == "ror") {
        if (!lt.isVector()) {
            unsupportedError(flp, "shift or rotate of a non-vector value");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (!rt.isInt()) {
            flp->v3error("VHDL: shift count is not an integer");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (op == "rol" || op == "ror") return makeRotate(flp, lp, rp, op == "rol");
        // IEEE 1076-2008 9.2.3 gives the predefined shift operators bit and boolean
        // elements; std_logic_1164 (2008) declares the std_logic_vector ones, so a
        // std_logic_vector shifts without a numeric package
        // A negative count is the opposite shift (IEEE 1076-2008 9.2.4)
        const std::string oppositeOp = op == "sll"   ? "srl"
                                       : op == "srl" ? "sll"
                                       : op == "sla" ? "sra"
                                                     : "sla";
        int64_t count = 0;
        if (staticIntFold(expr.b, count)) {
            if (count < 0) {
                rp->deleteTree();
                rp = makeIntConst(flp, -count);
                return makeShift(flp, lp, rp, oppositeOp, lt);
            }
            return makeShift(flp, lp, rp, op, lt);
        }
        // A dynamic count selects its direction: the count itself when it is
        // non-negative, and `0 - count`, which is non-negative, the other way
        AstNodeExpr* const testp = new AstGteS{flp, rp, makeIntConst(flp, 0)};
        testp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        AstNodeExpr* const negp = new AstSub{flp, makeIntConst(flp, 0), rp->cloneTree(false)};
        negp->dtypep(intType(flp).dtp);
        AstNodeExpr* const forwardp
            = makeShift(flp, lp->cloneTree(false), rp->cloneTree(false), op, lt);
        AstNodeExpr* const backwardp = makeShift(flp, lp, negp, oppositeOp, lt);
        AstCond* const resultp = new AstCond{flp, testp, forwardp, backwardp};
        resultp->dtypep(forwardp->dtypep());
        return resultp;
    }
    if (op == "**") {
        // A static exponent unrolls into a chain of multiplications
        int64_t exponent = 0;
        if (!staticIntFold(expr.b, exponent)) {
            // A power of two with a run-time exponent is a shift, which is how a
            // generic-derived width turns into a decoder size
            int64_t base = 0;
            if (staticIntFold(expr.a, base) && base == 2 && lt.isInt()) {
                AstNodeExpr* const shiftp = new AstShiftL{flp, makeIntConst(flp, 1), rp, lt.width};
                shiftp->dtypep(lt.dtp);
                lp->deleteTree();
                return shiftp;
            }
            unsupportedError(flp, "non-static exponent");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        if (exponent < 0 || exponent > 64) {
            unsupportedError(flp, "exponent out of range");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        rp->deleteTree();
        if (!lt.isInt()) {
            unsupportedError(flp, "exponentiation of a non-integer value");
            lp->deleteTree();
            return nullptr;
        }
        if (exponent == 0) {
            lp->deleteTree();
            return makeIntConst(flp, 1);
        }
        AstNodeExpr* resultp = lp->cloneTree(false);
        for (int64_t i = 1; i < exponent; ++i) {
            AstNodeExpr* const mulp = new AstMul{flp, resultp, lp->cloneTree(false)};
            mulp->dtypep(lt.dtp);
            resultp = mulp;
        }
        lp->deleteTree();
        return resultp;
    }

    const bool arithmetic
        = op == "+" || op == "-" || op == "*" || op == "/" || op == "mod" || op == "rem";
    if (!arithmetic) {
        unsupportedError(flp, "operator '" + op + "'");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    if (lt.isInt() && rt.isInt()) {
        // IEEE 1076-2008 9.1: dividing by zero is an error (subset row 4.5)
        if (op == "/" || op == "mod" || op == "rem") {
            int64_t divisor = 0;
            if (evalInt(*expr.b, divisor) && divisor == 0) {
                flp->v3error("VHDL: division by zero");
                lp->deleteTree();
                rp->deleteTree();
                return nullptr;
            }
            if (m_modp) {
                // A dynamic divisor can reach zero at run time, where the standard
                // requires an error rather than a silent zero. Test the whole value with a
                // reduction so the operand's width and signedness need no constant.
                AstRedOr* const anyBitp = new AstRedOr{flp, rp->cloneTree(false)};
                anyBitp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
                AstNodeExpr* const zeroTestp = new AstNot{flp, anyBitp};
                zeroTestp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
                addRuntimeError(flp, zeroTestp, "VHDL: division by zero");
            }
        }
        AstNodeExpr* resultp = nullptr;
        // The operands are both integers but need not have the same width (time is 64 bits,
        // integer 32), and the width pass requires equal widths, so extend the narrower one
        if (lt.width != rt.width) {
            const int wideWidth = std::max(lt.width, rt.width);
            if (lt.width < wideWidth) {
                AstNodeExpr* const extendedp
                    = lt.dtp->isSigned() || rt.dtp->isSigned()
                          ? static_cast<AstNodeExpr*>(new AstExtendS{flp, lp, wideWidth})
                          : static_cast<AstNodeExpr*>(new AstExtend{flp, lp, wideWidth});
                extendedp->dtypep(rt.dtp);
                lp = extendedp;
            } else {
                AstNodeExpr* const extendedp
                    = lt.dtp->isSigned() || rt.dtp->isSigned()
                          ? static_cast<AstNodeExpr*>(new AstExtendS{flp, rp, wideWidth})
                          : static_cast<AstNodeExpr*>(new AstExtend{flp, rp, wideWidth});
                extendedp->dtypep(lt.dtp);
                rp = extendedp;
            }
        }
        if (op == "+") {
            resultp = new AstAdd{flp, lp, rp};
        } else if (op == "-") {
            resultp = new AstSub{flp, lp, rp};
        } else if (op == "*") {
            resultp = new AstMul{flp, lp, rp};
        } else if (op == "rem") {
            // rem takes the dividend's sign, which is what the remainder gives
            resultp = makeRem(flp, lp, rp, lt.dtp, true);
        } else if (op == "mod") {
            resultp = makeMod(flp, lp, rp, lt.dtp, true);
        } else {
            resultp = new AstDivS{flp, lp, rp};
        }
        resultp->dtypep(lt.dtp);
        // A division's width follows its widest operand in the width pass, while VHDL's
        // result is the dividend's type (dividing two physical values, or a physical value
        // by an integer, gives integer), so narrow to it explicitly
        if ((op == "/" || op == "mod" || op == "rem") && std::max(lt.width, rt.width) > lt.width) {
            AstNodeExpr* const narrowp = new AstSel{flp, resultp, 0, lt.width};
            narrowp->dtypep(lt.dtp);
            return narrowp;
        }
        return resultp;
    }
    // A static integer operand acts as a natural/signed literal of the vector operand's
    // type; a dynamic one is converted to that type, as numeric_std does
    if (lt.isInt() != rt.isInt()) {
        const Expr& intExpr = lt.isInt() ? *expr.a : *expr.b;
        const VType& vectorT = lt.isInt() ? rt : lt;
        int64_t value = 0;
        AstNodeExpr* operandp = nullptr;
        if (evalInt(intExpr, value)) {
            operandp = makeVectorConst(flp, vectorT, value);
        } else {
            operandp = resizeExpr(lt.isInt() ? lp : rp, vectorT, flp);
        }
        if (lt.isInt()) {
            lp = operandp;
            lt = vectorT;
        } else {
            rp = operandp;
            rt = vectorT;
        }
    }
    if (!lt.isVector() || !rt.isVector() || lt.kwd != rt.kwd) {
        unsupportedError(flp, "arithmetic on non-numeric operands");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    // Two vector operands must have one base type; a numeric_std unsigned and a
    // std_logic_vector do not mix (IEEE 1076-2008 9.2.5)
    if (!lt.typeName.empty() && !rt.typeName.empty()
        && !arrayBaseTypesRelated(lt.typeName, rt.typeName)) {
        flp->v3error("VHDL: arithmetic operands do not have the same base type");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    // IEEE 1076-2008 9.1: the predefined arithmetic operators have bit and boolean
    // elements, so a std_logic_vector needs one of the packages that declare them
    if (lt.kwd == VBasicDTypeKwd::LOGIC && lt.signing == VSigning::NOSIGN && !m_stdLogicArith
        && !m_stdLogicSigned && !m_stdLogicUnsigned) {
        flp->v3error("VHDL: arithmetic on std_logic_vector needs a package that defines it "
                     "(IEEE 1076-2008 9.1)");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    if (m_stdLogicSigned
        && ((lt.kind == VType::VECTOR && lt.kwd == VBasicDTypeKwd::LOGIC)
            || (rt.kind == VType::VECTOR && rt.kwd == VBasicDTypeKwd::LOGIC))) {
        unsupportedError(flp, "std_logic_signed arithmetic");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    if (lt.isSigned() != rt.isSigned()) {
        unsupportedError(flp, "arithmetic between signed and unsigned operands");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    const VSigning signing = lt.signing;
    int resultWidth = 0;
    if (op == "*") {
        resultWidth = lt.width + rt.width;
    } else if (op == "/" || op == "mod" || op == "rem") {
        resultWidth = lt.width;
    } else {
        resultWidth = std::max(lt.width, rt.width);
    }
    const int opWidth = std::max(resultWidth, std::max(lt.width, rt.width));
    const VType opType = vectorType(flp, lt.kwd, signing, opWidth);
    lp = resizeExpr(lp, opType, flp);
    rp = resizeExpr(rp, opType, flp);
    AstNodeExpr* resultp = nullptr;
    if (op == "+") {
        resultp = new AstAdd{flp, lp, rp};
    } else if (op == "-") {
        resultp = new AstSub{flp, lp, rp};
    } else if (op == "*") {
        resultp = new AstMul{flp, lp, rp};
    } else if (op == "rem") {
        resultp = makeRem(flp, lp, rp, lp->dtypep(), signing == VSigning::SIGNED);
    } else if (op == "mod") {
        resultp = makeMod(flp, lp, rp, lp->dtypep(), signing == VSigning::SIGNED);
    } else {
        resultp = signing == VSigning::SIGNED ? static_cast<AstNodeExpr*>(new AstDivS{flp, lp, rp})
                                              : static_cast<AstNodeExpr*>(new AstDiv{flp, lp, rp});
    }
    const VType resultType = vectorType(flp, lt.kwd, signing, resultWidth);
    if (resultWidth != opWidth) {
        AstSel* const selp = new AstSel{flp, resultp, 0, resultWidth};
        selp->dtypep(resultType.dtp);
        return selp;
    }
    resultp->dtypep(resultType.dtp);
    return resultp;
}

//! rising_edge/falling_edge register a clock edge and are true only when it fires
AstNodeExpr* Lowering::lowerEdgeCall(const Expr& expr) {
    FileLine* const flp = expr.flp;
    if (expr.args.size() != 1 || expr.args[0]->kind != Expr::NAME) {
        unsupportedError(flp, "edge function argument");
        return nullptr;
    }
    AstVar* const varp = lookupVar(*expr.args[0]);
    if (!varp) {
        flp->v3error("VHDL: unknown signal '" + expr.args[0]->spelling + "' in an edge function");
        return nullptr;
    }
    // The std_logic_1164 edge functions take a scalar std_ulogic (IEEE 1164 3.2.6);
    // a vector, an integer, a boolean or a bit has no such overload
    VType argType;
    if (actualType(*expr.args[0], argType)
        && (argType.kind != VType::LOGIC || argType.isVector() || argType.typeName == "boolean")) {
        flp->v3error("VHDL: " + expr.a->text + " operand is not std_ulogic");
        return nullptr;
    }
    const bool rising = expr.a->text == "rising_edge";
    Edge edge;
    edge.varp = varp;
    edge.type = rising ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
    m_edges.push_back(edge);
    // At time zero no signal has had an event
    if (m_initial) return bitConst(flp, '0');
    // With one sensitivity signal the always is change sensitive and runs on both
    // edges, so the edge call is the signal's level
    if (m_edgeAsLevel) return edgeLevel(flp, varp, rising);
    // With several sensitivity signals the always runs for any of them, so the edge
    // call must check that this signal itself changed since the previous run
    if (m_edgeAsPrev) return edgeFromChanged(flp, varp, rising);
    // Otherwise the sensitivity item supplies the edge
    return bitConst(flp, '1');
}

AstNodeExpr* Lowering::edgeLevel(FileLine* flp, AstVar* varp, bool rising) {
    AstNodeExpr* const refp = new AstVarRef{flp, varp, VAccess::READ};
    if (rising) return refp;
    AstNot* const notp = new AstNot{flp, refp};
    notp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    return notp;
}

AstNodeExpr* Lowering::edgeChanged(FileLine* flp, AstVar* varp) {
    AstVar*& prevp = m_edgePrevVars[varp];
    if (!prevp) {
        const std::string name = "vhdledge_" + std::to_string(++m_edgeSeq) + "__" + varp->name();
        prevp = new AstVar{flp, VVarType::VAR, name, varp->dtypep()};
        m_modp->addStmtsp(prevp);
    }
    AstNodeExpr* const changedp = new AstNeq{flp, new AstVarRef{flp, varp, VAccess::READ},
                                             new AstVarRef{flp, prevp, VAccess::READ}};
    changedp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    return changedp;
}

AstNodeExpr* Lowering::edgeFromChanged(FileLine* flp, AstVar* varp, bool rising) {
    AstNodeExpr* const andp
        = new AstAnd{flp, edgeChanged(flp, varp), edgeLevel(flp, varp, rising)};
    andp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    return andp;
}

//! The remainder operator, signed or unsigned
AstNodeExpr* Lowering::makeRem(FileLine* flp, AstNodeExpr* lp, AstNodeExpr* rp, AstNodeDType* dtp,
                               bool sign) {
    AstNodeExpr* const remp = sign ? static_cast<AstNodeExpr*>(new AstModDivS{flp, lp, rp})
                                   : static_cast<AstNodeExpr*>(new AstModDiv{flp, lp, rp});
    remp->dtypep(dtp);
    return remp;
}

//! VHDL mod takes the divisor's sign, so it is ((a % b) + b) % b
AstNodeExpr* Lowering::makeMod(FileLine* flp, AstNodeExpr* lp, AstNodeExpr* rp, AstNodeDType* dtp,
                               bool sign) {
    AstNodeExpr* const remp = makeRem(flp, lp, rp->cloneTree(false), dtp, sign);
    AstNodeExpr* const plusp = new AstAdd{flp, remp, rp};
    plusp->dtypep(dtp);
    return makeRem(flp, plusp, rp->cloneTree(false), dtp, sign);
}

//! One rotation of a vector: the low `count mod width` bits move to the other end
AstNodeExpr* Lowering::makeRotate(FileLine* flp, AstNodeExpr* valuep, AstNodeExpr* amountp,
                                  bool left) {
    AstNodeDType* const dtp = valuep->dtypep();
    const int width = dtp->width();
    const VType intT = intType(flp);
    AstNodeExpr* const nmodp = new AstModDiv{flp, amountp, makeIntConst(flp, width)};
    nmodp->dtypep(intT.dtp);
    AstNodeExpr* const restp = new AstSub{flp, makeIntConst(flp, width), nmodp->cloneTree(false)};
    restp->dtypep(intT.dtp);
    AstNodeExpr* const innerp
        = left ? static_cast<AstNodeExpr*>(
                     new AstShiftL{flp, valuep, nmodp->cloneTree(false), width})
               : static_cast<AstNodeExpr*>(
                     new AstShiftR{flp, valuep, nmodp->cloneTree(false), width});
    AstNodeExpr* const outerp
        = left ? static_cast<AstNodeExpr*>(
                     new AstShiftR{flp, valuep->cloneTree(false), restp, width})
               : static_cast<AstNodeExpr*>(
                     new AstShiftL{flp, valuep->cloneTree(false), restp, width});
    innerp->dtypep(dtp);
    outerp->dtypep(dtp);
    AstNodeExpr* const resultp = new AstOr{flp, innerp, outerp};
    resultp->dtypep(dtp);
    return resultp;
}

//! The shifted value for one direction, consuming its operands. A predefined
//! `bit_vector` or logic-vector shift fills the vacated positions with the
//! array's edge element: sra with the leftmost, sla with the rightmost (IEEE
//! 1076-2008 9.2.3). The numeric_std vector types keep their own shift semantics.
AstNodeExpr* Lowering::makeShift(FileLine* flp, AstNodeExpr* valuep, AstNodeExpr* amountp,
                                 const std::string& op, const VType& vt) {
    AstNodeDType* const dtp = valuep->dtypep();
    const int width = dtp->width();
    const bool edgeFill = vt.kwd == VBasicDTypeKwd::BIT || vt.isPredefinedVector();
    AstNodeExpr* resultp = nullptr;
    if (op == "sll") {
        resultp = new AstShiftL{flp, valuep, amountp, width};
    } else if (op == "sra") {
        if (edgeFill) {
            // VHDL's predefined sra fills with the leftmost element. Shift a signed view so
            // the committed width survives: V3Width turns an AstShiftRS on an unsigned
            // dtype into a logical shift.
            AstNodeDType* const signedDtp
                = findVector(flp, VBasicDTypeKwd::LOGIC, VSigning::SIGNED, width - 1, 0, false);
            AstNodeExpr* const shiftedp
                = new AstShiftRS{flp, new AstCast{flp, valuep, signedDtp}, amountp, width};
            shiftedp->dtypep(signedDtp);
            resultp = new AstCast{flp, shiftedp, dtp};
        } else {
            resultp = vt.isSigned()
                          ? static_cast<AstNodeExpr*>(new AstShiftRS{flp, valuep, amountp, width})
                          : static_cast<AstNodeExpr*>(new AstShiftR{flp, valuep, amountp, width});
        }
    } else if (op == "sla" && edgeFill) {
        // (value << n) | (rightmost_element & ((1 << n) - 1)). The mask subtraction
        // wraps for n >= width, which fills the whole result with the edge element.
        AstConst* const onep = new AstConst{flp, V3Number{flp, width, 1u}};
        onep->dtypep(dtp);
        AstSel* const lsbp = new AstSel{flp, valuep->cloneTree(false), 0, 1};
        lsbp->dtypep(findBasic(flp, VBasicDTypeKwd::BIT));
        AstReplicate* const fillValuep = new AstReplicate{flp, lsbp, static_cast<uint32_t>(width)};
        fillValuep->dtypep(dtp);
        AstNodeExpr* const maskShiftp
            = new AstShiftL{flp, onep->cloneTree(false), amountp->cloneTree(false), width};
        maskShiftp->dtypep(dtp);
        AstNodeExpr* const maskp = new AstSub{flp, maskShiftp, onep};
        maskp->dtypep(dtp);
        AstAnd* const fillp = new AstAnd{flp, fillValuep, maskp};
        fillp->dtypep(dtp);
        AstNodeExpr* const shiftedp = new AstShiftL{flp, valuep, amountp, width};
        shiftedp->dtypep(dtp);
        AstOr* const resultOrp = new AstOr{flp, shiftedp, fillp};
        resultOrp->dtypep(dtp);
        resultp = resultOrp;
    } else if (op == "sla") {
        resultp = new AstShiftL{flp, valuep, amountp, width};
    } else {
        resultp = new AstShiftR{flp, valuep, amountp, width};
    }
    resultp->dtypep(dtp);
    return resultp;
}

//! Standard-package functions that lower to an AST idiom, keyed by folded name
AstNodeExpr* Lowering::makeReduction(FileLine* flp, AstNodeExpr* operandp, const std::string& op) {
    const VType type = typeOfDType(operandp->dtypep());
    AstNodeExpr* resultp = nullptr;
    if (op == "and" || op == "nand") {
        resultp = new AstRedAnd{flp, operandp};
    } else if (op == "or" || op == "nor") {
        resultp = new AstRedOr{flp, operandp};
    } else {
        resultp = new AstRedXor{flp, operandp};
    }
    AstNodeDType* const bitDtp = findBasic(
        flp, type.kwd == VBasicDTypeKwd::BIT ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC);
    resultp->dtypep(bitDtp);
    if (op == "nand" || op == "nor" || op == "xnor") {
        AstNot* const notp = new AstNot{flp, resultp};
        notp->dtypep(bitDtp);
        return notp;
    }
    return resultp;
}

AstNodeExpr* Lowering::lowerIntrinsic(const Expr& expr) {
    FileLine* const flp = expr.flp;
    const std::string& fn = expr.a->text;
    const std::vector<ExprP>& argsp = expr.args;
    // The model is 2-state, so no value can be a metavalue
    if (fn == "is_x" || fn == "is_x01" || fn == "is_x01z" || fn == "is_ux01") {
        return bitConst(flp, '0');
    }
    // The model is 2-state, so every value is already 0 or 1 and the X01 mapping
    // of the X/Z/don't-care values is the identity
    if (fn == "to_x01" || fn == "to_01" || fn == "to_x01z" || fn == "to_ux01") {
        if (argsp.size() != 1) {
            unsupportedError(flp, "function '" + fn + "' without one argument");
            return nullptr;
        }
        return lowerExpr(*argsp[0], nullptr);
    }
    // `std_match(a, b)`: a '-' bit of either operand matches anything, so the operands
    // are compared under a mask of the bits that are not wildcards. The model is
    // 2-state, so a metavalue is a 0 on both sides and compares equal
    if (fn == "std_match") {
        if (argsp.size() != 2) {
            unsupportedError(flp, "std_match without two arguments");
            return nullptr;
        }
        AstNodeExpr* lp = lowerExpr(*argsp[0], nullptr);
        if (!lp) return nullptr;
        AstNodeExpr* rp = lowerExpr(*argsp[1], nullptr);
        if (!rp) {
            lp->deleteTree();
            return nullptr;
        }
        const int width = std::max(lp->width(), rp->width());
        std::string mask(static_cast<size_t>(width), '1');
        for (const ExprP& argp : argsp) {
            if (!argp || argp->kind != Expr::BITS_LIT) continue;
            const size_t argWidth = argp->bits.size();
            for (size_t i = 0; i < argWidth && i < static_cast<size_t>(width); ++i) {
                if (argp->bits[i] == '-') mask[i + static_cast<size_t>(width) - argWidth] = '0';
            }
        }
        // A don't-care can also arrive through a constant: its '-' bits are x bits in
        // the lowered value, whatever name or expression carried them here
        for (AstNodeExpr* const sidep : {lp, rp}) {
            const AstConst* const constp = VN_CAST(sidep, Const);
            if (!constp) continue;
            for (int b = 0; b < constp->width() && b < width; ++b) {
                if (!constp->num().bitIsX(b)) continue;
                mask[static_cast<size_t>(width - 1 - b)] = '0';
            }
        }
        const VType vec = vectorType(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, width);
        AstNodeExpr* const maskp = makeBitsConst(flp, vec.dtp, mask);
        lp = resizeExpr(lp, vec, flp);
        rp = resizeExpr(rp, vec, flp);
        AstNodeExpr* const leftp = new AstAnd{flp, lp, maskp->cloneTree(false)};
        leftp->dtypep(vec.dtp);
        AstNodeExpr* const rightp = new AstAnd{flp, rp, maskp};
        rightp->dtypep(vec.dtp);
        AstNodeExpr* const eqp = new AstEq{flp, leftp, rightp};
        eqp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        return eqp;
    }
    if (argsp.empty()) {
        unsupportedError(flp, "function '" + fn + "' without an argument");
        return nullptr;
    }
    if (fn == "and_reduce" || fn == "or_reduce" || fn == "xor_reduce" || fn == "nand_reduce"
        || fn == "nor_reduce" || fn == "xnor_reduce") {
        if (argsp.size() != 1) {
            unsupportedError(flp, "function '" + fn + "' without one argument");
            return nullptr;
        }
        AstNodeExpr* const operandp = lowerExpr(*argsp[0], nullptr);
        if (!operandp) return nullptr;
        return makeReduction(flp, operandp, fn.substr(0, fn.find("_reduce")));
    }
    if ((fn == "resize" || fn == "conv_std_logic_vector" || fn == "conv_unsigned"
         || fn == "conv_signed")
        && argsp.size() == 2) {
        AstNodeExpr* valuep = lowerExpr(*argsp[0], nullptr);
        if (!valuep) return nullptr;
        int64_t width = 0;
        if (!staticInt(argsp[1], width) || width <= 0 || width > 4096) {
            unsupportedError(flp, "non-static width in '" + fn + "'");
            valuep->deleteTree();
            return nullptr;
        }
        const VType valueType = typeOfDType(valuep->dtypep());
        VBasicDTypeKwd kwd = valueType.kwd;
        VSigning signing = valueType.signing;
        if (fn != "resize") {
            kwd = VBasicDTypeKwd::LOGIC;
            if (fn == "conv_std_logic_vector") {
                signing = VSigning::NOSIGN;
            } else {
                signing = fn == "conv_signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
            }
        }
        const VType target = vectorType(flp, kwd, signing, static_cast<int>(width));
        return resizeExpr(valuep, target, flp);
    }
    if (fn == "shift_left" || fn == "shift_right" || fn == "shl" || fn == "shr") {
        if (argsp.size() != 2) {
            unsupportedError(flp, "function '" + fn + "' without two arguments");
            return nullptr;
        }
        AstNodeExpr* valuep = lowerExpr(*argsp[0], nullptr);
        AstNodeExpr* amountp = valuep ? lowerExpr(*argsp[1], nullptr) : nullptr;
        if (!valuep || !amountp) {
            if (valuep) valuep->deleteTree();
            if (amountp) amountp->deleteTree();
            return nullptr;
        }
        AstNodeDType* const dtp = valuep->dtypep();
        AstNodeExpr* resultp = nullptr;
        // shift_right sign-extends a signed operand; shift_left does not.
        // std_logic_arith's shl/shr are logical for both signednesses.
        if (fn == "shift_left" || fn == "shl") {
            resultp = new AstShiftL{flp, valuep, amountp, dtp->width()};
        } else if (fn == "shift_right" && dtp->isSigned()) {
            resultp = new AstShiftRS{flp, valuep, amountp, dtp->width()};
        } else {
            resultp = new AstShiftR{flp, valuep, amountp, dtp->width()};
        }
        resultp->dtypep(dtp);
        return resultp;
    }
    if (fn == "rotate_left" || fn == "rotate_right") {
        if (argsp.size() != 2) {
            unsupportedError(flp, "function '" + fn + "' without two arguments");
            return nullptr;
        }
        AstNodeExpr* const valuep = lowerExpr(*argsp[0], nullptr);
        AstNodeExpr* const amountp = valuep ? lowerExpr(*argsp[1], nullptr) : nullptr;
        if (!valuep || !amountp) {
            if (valuep) valuep->deleteTree();
            if (amountp) amountp->deleteTree();
            return nullptr;
        }
        return makeRotate(flp, valuep, amountp, fn == "rotate_left");
    }
    if (fn == "minimum" || fn == "maximum") {
        if (argsp.size() != 2) {
            unsupportedError(flp, "function '" + fn + "' without two arguments");
            return nullptr;
        }
        AstNodeExpr* const lhsp = lowerExpr(*argsp[0], nullptr);
        if (!lhsp) return nullptr;
        const VType lhsType = typeOfDType(lhsp->dtypep());
        AstNodeExpr* const rhsp = lowerExpr(*argsp[1], &lhsType);
        if (!rhsp) {
            lhsp->deleteTree();
            return nullptr;
        }
        AstNodeDType* const dtp = lhsp->dtypep();
        const bool sign = dtp->isSigned();
        AstNodeExpr* cmpp = nullptr;
        if (fn == "minimum") {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstLtS{flp, lhsp, rhsp})
                        : static_cast<AstNodeExpr*>(new AstLt{flp, lhsp, rhsp});
        } else {
            cmpp = sign ? static_cast<AstNodeExpr*>(new AstGtS{flp, lhsp, rhsp})
                        : static_cast<AstNodeExpr*>(new AstGt{flp, lhsp, rhsp});
        }
        cmpp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        AstCond* const resultp
            = new AstCond{flp, cmpp, lhsp->cloneTree(false), rhsp->cloneTree(false)};
        resultp->dtypep(dtp);
        return resultp;
    }
    if (fn == "conv_integer" || fn == "to_stdulogic" || fn == "to_bit") {
        AstNodeExpr* valuep = lowerExpr(*argsp[0], nullptr);
        if (!valuep) return nullptr;
        if (fn == "conv_integer") return resizeExpr(valuep, intType(flp), flp);
        // A scalar cast keeps the value and only changes the element type
        const VType valueType = typeOfDType(valuep->dtypep());
        const VType target
            = vectorType(flp, fn == "to_bit" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC,
                         VSigning::NOSIGN, valueType.width);
        return resizeExpr(valuep, target, flp);
    }
    if (fn == "to_stdlogicvector" || fn == "to_stdulogicvector" || fn == "to_bitvector") {
        AstNodeExpr* valuep = lowerExpr(*argsp[0], nullptr);
        if (!valuep) return nullptr;
        const VType valueType = typeOfDType(valuep->dtypep());
        const VType target
            = vectorType(flp, fn == "to_bitvector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC,
                         VSigning::NOSIGN, valueType.width);
        return resizeExpr(valuep, target, flp);
    }
    unsupportedError(flp, "function '" + fn + "'");
    return nullptr;
}

AstNodeExpr* Lowering::lowerConversion(const Expr& expr, const VType* context) {
    FileLine* const flp = expr.flp;
    const std::string& name = expr.a->text;
    if (expr.args.size() == 2) {
        if (name == "to_unsigned" || name == "to_signed") {
            int64_t width = 0;
            const bool staticWidth
                = staticIntFold(expr.args[1], width) && width > 0 && width <= 4096;
            // A width only the consumer knows takes that width: to_unsigned pads with
            // zeros, and the elements beyond the run-time width are never selected
            if (!staticWidth && name == "to_unsigned" && context && context->isVector()
                && context->width > 0) {
                AstNodeExpr* const operandp = lowerExpr(*expr.args[0], nullptr);
                if (!operandp) return nullptr;
                if (!typeOfDType(operandp->dtypep()).isInt()) {
                    unsupportedError(flp, "to_unsigned of a non-integer value");
                    operandp->deleteTree();
                    return nullptr;
                }
                return resizeExpr(operandp, *context, flp);
            }
            AstNodeExpr* valuep = lowerExpr(*expr.args[0], nullptr);
            if (!valuep) return nullptr;
            const VType valueType = typeOfDType(valuep->dtypep());
            if (!staticWidth) {
                unsupportedError(flp, "non-static or invalid conversion width");
                valuep->deleteTree();
                return nullptr;
            }
            if (!valueType.isInt()) {
                unsupportedError(flp, name + " of a non-integer value");
                valuep->deleteTree();
                return nullptr;
            }
            const VType target
                = vectorType(flp, VBasicDTypeKwd::LOGIC,
                             name == "to_signed" ? VSigning::SIGNED : VSigning::UNSIGNED,
                             static_cast<int>(width));
            return resizeExpr(valuep, target, flp);
        }
        unsupportedError(flp, "conversion '" + name + "' with two arguments");
        return nullptr;
    }
    if (expr.args.size() != 1) {
        unsupportedError(flp, "conversion '" + name + "' with " + std::to_string(expr.args.size())
                                  + " arguments");
        return nullptr;
    }
    // A constant operand folds first: a real expression inside it never reaches the
    // model, which has no real type, and the conversion of a constant is a constant.
    // A vector-valued designator must not fold to an integer, or the conversion would
    // see a non-vector value.
    int64_t folded = 0;
    VType operandType;
    const bool vectorOperand
        = typeOfDesignator(*expr.args[0], operandType) && operandType.isVector();
    // An operand that is itself a conversion, or a call whose return type is
    // unconstrained, has no width of its own: it takes this conversion's context
    bool operandNeedsContext = false;
    if (expr.args[0]->kind == Expr::INDEX && expr.args[0]->a) {
        const std::string& callee = expr.args[0]->a->text;
        operandNeedsContext = isConversion(callee);
        if (!operandNeedsContext && m_subprograms.count(callee)) {
            for (const SubprogramDecl* const subp : visibleSubprograms(callee)) {
                const std::string& retMark = subp->returnType.mark;
                const bool unconstrainedRet
                    = (retMark == "std_logic_vector" || retMark == "std_ulogic_vector"
                       || retMark == "bit_vector" || retMark == "unsigned" || retMark == "signed"
                       || retMark == "u_unsigned" || retMark == "u_signed" || retMark == "string")
                      && !subp->returnType.hasRange && subp->returnType.constraintMark.empty();
                if (unconstrainedRet) operandNeedsContext = true;
            }
        }
    }
    AstNodeExpr* valuep = (!vectorOperand && evalInt(*expr.args[0], folded))
                              ? makeIntConst(flp, folded)
                              : lowerExpr(*expr.args[0], operandNeedsContext ? context : nullptr);
    if (!valuep) return nullptr;
    UASSERT_OBJ(valuep->dtypep(), valuep, "VHDL conversion operand has no data type");
    const VType valueType = typeOfDType(valuep->dtypep());
    // A type conversion needs closely related types. A conversion mark is not a
    // subprogram, so `formalType` resolving it is what distinguishes the two.
    {
        TypeSpec targetSpec;
        targetSpec.flp = flp;
        targetSpec.mark = name;
        VType targetType;
        VType sourceType = valueType;
        VType actual;
        if (actualType(*expr.args[0], actual) && !actual.typeName.empty()) sourceType = actual;
        if (formalType(targetSpec, targetType) && !conversionRelated(targetType, sourceType)) {
            flp->v3error("VHDL: type conversion '" + name + "' from an unrelated type");
            valuep->deleteTree();
            return nullptr;
        }
    }
    if (name == "to_integer") {
        // numeric_std declares to_integer only for unsigned and signed (IEEE 1076-2008
        // 16.8.5); an integer or a std_logic_vector operand is not a conversion
        if (!valueType.isVector()
            || (valueType.signing != VSigning::UNSIGNED
                && valueType.signing != VSigning::SIGNED)) {
            flp->v3error("VHDL: to_integer operand is not unsigned or signed");
            valuep->deleteTree();
            return nullptr;
        }
        if (valueType.width > 32) {
            unsupportedError(flp, name + " of this operand");
            valuep->deleteTree();
            return nullptr;
        }
        return resizeExpr(valuep, intType(flp), flp);
    }
    if (name == "integer" || name == "natural" || name == "positive") {
        if (valueType.isInt()) return valuep;
        if (!valueType.isVector() || valueType.width > 32) {
            unsupportedError(flp, name + " of this operand");
            valuep->deleteTree();
            return nullptr;
        }
        return resizeExpr(valuep, intType(flp), flp);
    }
    if (!valueType.isVector()) {
        // A conversion of a value whose width only the consumer knows, e.g. one that
        // holds a run-time-width to_unsigned: the packed form is the same
        if (context && context->isVector() && context->width > 0 && valueType.isInt()) {
            return resizeExpr(valuep, *context, flp);
        }
        unsupportedError(flp, "conversion of a non-vector value");
        valuep->deleteTree();
        return nullptr;
    }
    VBasicDTypeKwd kwd = valueType.kwd;
    VSigning signing = VSigning::NOSIGN;
    if (name == "signed" || name == "u_signed") {
        signing = VSigning::SIGNED;
    } else if (name == "unsigned" || name == "u_unsigned") {
        signing = VSigning::UNSIGNED;
    } else if (name == "bit_vector") {
        kwd = VBasicDTypeKwd::BIT;
    }
    const VType target = vectorType(flp, kwd, signing, valueType.width);
    valuep->dtypep(target.dtp);
    return valuep;
}

AstNodeExpr* Lowering::lowerDesignator(const Expr& expr, bool write) {
    FileLine* const flp = expr.flp;
    if (expr.kind == Expr::NAME) {
        if (activeEnv().count(expr.text)) return inlineBinding(flp, expr.text, write);
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) {
            if (write) {
                flp->v3error("VHDL: cannot assign to constant '" + expr.spelling + "'");
                return nullptr;
            }
            return makeIntConst(flp, constIt->second);
        }
        const auto preIt = m_preConsts.find(expr.text);
        if (preIt != m_preConsts.end()) {
            if (write) {
                flp->v3error("VHDL: cannot assign to constant '" + expr.spelling + "'");
                return nullptr;
            }
            return makeIntConst(flp, preIt->second);
        }
        if (m_ambiguousLiterals.count(expr.text)) {
            flp->v3error("VHDL: enumeration literal '" + expr.spelling + "' is ambiguous");
            return nullptr;
        }
        const auto valueIt = m_constValues.find(expr.text);
        if (valueIt != m_constValues.end() || m_enumConsts.count(expr.text)) {
            if (write) {
                flp->v3error("VHDL: cannot assign to constant '" + expr.spelling + "'");
                return nullptr;
            }
            AstConst* const constp = valueIt != m_constValues.end()
                                         ? valueIt->second
                                         : m_enumConsts.find(expr.text)->second;
            return constp->cloneTree(false);
        }
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            if (AstNodeExpr* const aliasp = lowerAlias(expr, write)) return aliasp;
            flp->v3error("VHDL: unknown identifier '" + expr.spelling + "'");
            return nullptr;
        }
        AstVar* const varp = it->second.varp;
        if (write && varp->isInput()) {
            flp->v3error("VHDL: cannot assign to input port '" + expr.spelling + "'");
            return nullptr;
        }
        return new AstVarRef{flp, varp, write ? VAccess::WRITE : VAccess::READ};
    }
    if (expr.kind == Expr::INDEX || expr.kind == Expr::SLICE) {
        if (!expr.a
            || (expr.a->kind != Expr::NAME && expr.a->kind != Expr::INDEX
                && expr.a->kind != Expr::SLICE && expr.a->kind != Expr::SELECTED
                && expr.a->kind != Expr::CALL)) {
            unsupportedError(flp, "selection of a non-name expression");
            return nullptr;
        }
        // A slice of a function result is an expression, not a variable designator
        const bool expressionPrefix
            = expr.a->kind == Expr::CALL
              || (expr.a->kind == Expr::INDEX && expr.a->a && expr.a->a->kind == Expr::NAME
                  && (m_subprograms.count(expr.a->a->text) || isIntrinsic(expr.a->a->text)
                      || isConversion(expr.a->a->text) || m_types.count(expr.a->a->text)));
        AstNodeExpr* const basep
            = expressionPrefix ? lowerExpr(*expr.a, nullptr) : lowerDesignator(*expr.a, write);
        if (!basep) return nullptr;
        // Prefer the declared type: it keeps the element width of an array type
        VType baseType;
        if (!typeOfDesignator(*expr.a, baseType)) {
            // An array a function returns is typed by its declared return type, which the
            // returned value's own data type no longer carries: `f(x)(0)` selects an
            // element, not a bit. A call with arguments reaches here as an index whose
            // prefix is the name
            const bool callPrefix
                = expr.a->kind == Expr::CALL
                  || (expr.a->kind == Expr::INDEX && expr.a->a && expr.a->a->kind == Expr::NAME
                      && m_subprograms.count(expr.a->a->text));
            const SubprogramDecl* const subp
                = callPrefix ? resolveCall(*expr.a, true, nullptr) : nullptr;
            const auto typeIt = subp ? m_types.find(subp->returnType.mark) : m_types.end();
            if (subp && typeIt != m_types.end() && typeIt->second.kind == VType::ARRAY
                && !subp->returnType.hasRange && subp->returnType.constraintMark.empty()) {
                baseType = typeIt->second;  // The element width survives only in the type
            } else {
                baseType = typeOfDType(basep->dtypep());
            }
        }
        return lowerSel(expr, basep, baseType, write);
    }
    if (expr.kind == Expr::SELECTED) {
        VType prefixType;
        if (!expr.a || !typeOfDesignator(*expr.a, prefixType)
            || prefixType.kind != VType::RECORD) {
            unsupportedError(flp, "record element as an assignment target");
            return nullptr;
        }
        for (const VField& field : prefixType.fields) {
            if (field.name != expr.text) continue;
            AstNodeExpr* const basep = lowerDesignator(*expr.a, write);
            if (!basep) return nullptr;
            AstSel* const selp = new AstSel{flp, basep, field.offset, field.width};
            selp->dtypep(field.type->dtp);
            return selp;
        }
        flp->v3error("VHDL: record has no element '" + expr.spelling + "'");
        return nullptr;
    }
    unsupportedError(flp, "expression as an assignment target");
    return nullptr;
}
bool Lowering::sliceBound(const ExprP& exprp, const std::string& prefix, const VType& baseType,
                          int64_t& out) {
    // A variable that was assigned a static value earlier counts as one here, as does the
    // loop constant it was computed from
    const ExprP boundp = substVars(*exprp);
    // A bound can name a constant whose value only a folded call provides, e.g. a slice of
    // a fixed number of address bits next to a base address
    if (staticIntFold(boundp, out)) return true;
    if (boundp->kind == Expr::PAREN && boundp->a) {
        return sliceBound(boundp->a, prefix, baseType, out);
    }
    if (boundp->kind == Expr::ATTR && boundp->a && boundp->a->spelling == prefix) {
        if (boundp->text == "left") {
            out = baseType.left;
            return true;
        }
        if (boundp->text == "right") {
            out = baseType.right;
            return true;
        }
        if (boundp->text == "length") {
            out = std::abs(baseType.left - baseType.right) + 1;
            return true;
        }
        return false;
    }
    if (boundp->kind == Expr::BINARY && (boundp->text == "+" || boundp->text == "-") && boundp->a
        && boundp->b) {
        int64_t lhs = 0;
        int64_t rhs = 0;
        if (!sliceBound(boundp->a, prefix, baseType, lhs)) return false;
        if (!staticIntFold(boundp->b, rhs)) return false;
        out = boundp->text == "+" ? lhs + rhs : lhs - rhs;
        return true;
    }
    return false;
}

AstNodeExpr* Lowering::lowerSel(const Expr& expr, AstNodeExpr* basep, const VType& baseType,
                                bool write) {
    (void)write;
    FileLine* const flp = expr.flp;
    if (!baseType.isVector()) {
        unsupportedError(flp, "index or slice of a non-array value");
        basep->deleteTree();
        return nullptr;
    }
    // A VECTOR is an array of a scalar element type; an ARRAY names its element type
    VType elemType;
    if (baseType.kind == VType::ARRAY && baseType.elemp) {
        elemType = *baseType.elemp;
    } else {
        elemType.kind = baseType.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
        elemType.dtp = findBasic(flp, baseType.kwd);
    }
    const int elemWidth = baseType.elemWidth;
    if (elemWidth <= 0) {
        // An array of unconstrained elements has no element width to select
        unsupportedError(flp, "index of an array with an unconstrained element");
        basep->deleteTree();
        return nullptr;
    }
    // Only `prefix'range` is a slice by the prefix's own bounds. Any other attribute used
    // as an index (`v(x'high)`) or slice bound (`v(x'high downto 0)`) lowers as an expression
    const auto rangeAttr = [](const ExprP& e) -> const Expr* {
        return e && e->kind == Expr::ATTR && e->text == "range" ? e.get() : nullptr;
    };
    const Expr* rangep = expr.kind == Expr::INDEX && expr.args.size() == 1
                             ? rangeAttr(expr.args[0])
                             : rangeAttr(expr.b);
    if (rangep) {
        if (!rangep->a || rangep->a->kind != Expr::NAME) {
            unsupportedError(flp, "'range of a non-name prefix");
            basep->deleteTree();
            return nullptr;
        }
        // The bounds come from the prefix name's type, not from the sliced array
        const VType* const prefixTypep = typeOfName(*rangep->a);
        if (!prefixTypep) {
            flp->v3error("VHDL: unknown name '" + rangep->a->spelling + "' in 'range");
            basep->deleteTree();
            return nullptr;
        }
        // The attribute's own index selects which of the prefix's ranges is meant
        const VType* const levelp = indexRangeLevel(*rangep, *prefixTypep);
        if (!levelp) {
            unsupportedError(flp, "index of attribute 'range'");
            basep->deleteTree();
            return nullptr;
        }
        int hi = levelp->ascending ? levelp->right : levelp->left;
        int lo = levelp->ascending ? levelp->left : levelp->right;
        if (hi < lo) std::swap(hi, lo);
        return sliceOf(flp, basep, baseType, elemWidth, hi, lo, levelp->ascending);
    }
    if (expr.kind == Expr::INDEX) {
        // A single name argument that denotes a declared subtype is a discrete range
        // rather than a value: `v(some_range_t)` selects the elements those bounds denote
        // (IEEE 1800-2023 9.5.4). An object name is not a discrete range, so `v(i)` still
        // indexes; the subtype's direction decides whether the selected elements come out
        // reversed, which `sliceOf` already handles.
        if (expr.args.size() == 1) {
            if (const VType* const markp = discreteRangeType(*expr.args[0])) {
                if (markp->ascending != baseType.ascending) {
                    flp->v3error("VHDL: slice direction does not match the array");
                    basep->deleteTree();
                    return nullptr;
                }
                const int hi = std::max(markp->left, markp->right);
                const int lo = std::min(markp->left, markp->right);
                return sliceOf(flp, basep, baseType, elemWidth, hi, lo, markp->ascending);
            }
        }
        // A multi-dimensional index `m(i, j)` descends one array level per argument; a
        // nested `m(i)(j)` yields one single-argument INDEX per level and shares this path
        AstNodeExpr* curp = basep;
        VType levelType = baseType;
        for (size_t ai = 0; ai < expr.args.size(); ++ai) {
            if (!levelType.isVector()) {
                unsupportedError(flp, "index into a non-array value");
                curp->deleteTree();
                return nullptr;
            }
            // A second comma index would descend into a flat vector element. An array of
            // vectors is one-dimensional, so `v(i, j)` has too many indices; the valid form
            // is the chained `v(i)(j)`, which reaches here as separate single-index steps
            if (ai > 0 && levelType.kind == VType::VECTOR) {
                unsupportedError(flp, "multi-dimensional index on a vector element");
                curp->deleteTree();
                return nullptr;
            }
            VType levelElem;
            if (levelType.kind == VType::ARRAY && levelType.elemp) {
                levelElem = *levelType.elemp;
            } else {
                levelElem.kind = levelType.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
                levelElem.dtp = findBasic(flp, levelType.kwd);
            }
            const int levelWidth = levelType.elemWidth;
            AstNodeExpr* indexp = lowerExpr(*expr.args[ai], nullptr);
            if (!indexp) {
                curp->deleteTree();
                return nullptr;
            }
            if (!typeOfDType(indexp->dtypep()).isInt()) {
                // An enumeration value as an index is its position (IEEE 1076-2008 9.3.6):
                // a literal, a constant, or a formal an inlined body bound to one
                int64_t position = 0;
                if (!evalInt(*expr.args[ai], position)) {
                    unsupportedError(flp, "non-integer index");
                    indexp->deleteTree();
                    curp->deleteTree();
                    return nullptr;
                }
                indexp->deleteTree();
                indexp = makeIntConst(flp, position);
            }
            // The right bound is the least significant element in either direction
            AstNodeExpr* offsetp = indexp;
            if (levelType.ascending) {
                offsetp = new AstSub{flp, makeIntConst(flp, levelType.right), indexp};
            } else if (levelType.right != 0) {
                offsetp = new AstSub{flp, indexp, makeIntConst(flp, levelType.right)};
            }
            offsetp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
            if (levelWidth != 1) {
                AstNodeExpr* const scaledp
                    = new AstMul{flp, offsetp, makeIntConst(flp, levelWidth)};
                scaledp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
                offsetp = scaledp;
            }
            AstSel* const selp = new AstSel{flp, curp, offsetp, levelWidth};
            selp->dtypep(levelElem.dtp);
            curp = selp;
            levelType = levelElem;
        }
        return curp;
    }
    // A bound may be an attribute of the sliced prefix itself, e.g. `s(s'left-1 downto 1)`:
    // the prefix's own type fixes it, so it needs no scope lookup of a record element
    const auto boundValue = [&](const ExprP& bound, int64_t& out) -> bool {
        return sliceBound(bound, expr.a ? expr.a->spelling : std::string(), baseType, out);
    };
    int64_t leftValue = 0;
    int64_t rightValue = 0;
    if (boundValue(expr.b, leftValue) && boundValue(expr.c, rightValue)) {
        int hi = 0;
        int lo = 0;
        if (expr.text == "downto") {
            hi = static_cast<int>(leftValue);
            lo = static_cast<int>(rightValue);
        } else {
            lo = static_cast<int>(leftValue);
            hi = static_cast<int>(rightValue);
        }
        if (hi < lo) {
            flp->v3error("VHDL: empty or reversed slice");
            basep->deleteTree();
            return nullptr;
        }
        // A literal range in the other direction stays the documented extension `t_vhdl_range`
        // pins: its left bound is the most significant element
        return sliceOf(flp, basep, baseType, elemWidth, hi, lo, expr.text != "downto");
    }
    // A dynamic slice position keeps the width static and runs in the base's
    // direction, so the selected elements need no reordering
    const bool ascendingSlice = expr.text != "downto";
    if (ascendingSlice != baseType.ascending) {
        unsupportedError(flp, "slice against the array direction with non-static bounds");
        basep->deleteTree();
        return nullptr;
    }
    int64_t count = 0;
    if (!sliceWidth(*expr.b, *expr.c, ascendingSlice, count)) {
        unsupportedError(flp, "non-static slice width");
        basep->deleteTree();
        return nullptr;
    }
    AstNodeExpr* rightp = lowerExpr(*expr.c, nullptr);
    if (!rightp) {
        basep->deleteTree();
        return nullptr;
    }
    // The right operand is the selection's least significant element
    AstNodeExpr* offsetp = rightp;
    if (baseType.ascending) {
        offsetp = new AstSub{flp, makeIntConst(flp, baseType.right), rightp};
    } else if (baseType.right != 0) {
        offsetp = new AstSub{flp, rightp, makeIntConst(flp, baseType.right)};
    }
    offsetp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
    const int width = static_cast<int>(count) * elemWidth;
    AstSel* const selp = new AstSel{flp, basep, offsetp, width};
    selp->dtypep(findVectorWidth(flp, baseType.kwd, baseType.signing, width));
    return selp;
}

//! Slice assignment with a run-time width: VHDL bounds the result by the source, so every
//! width the arrays allow gets one guarded write with a static width and a run-time offset
AstNodeStmt* Lowering::lowerDynamicSliceAssign(const Stmt& stmt, bool variable) {
    FileLine* const flp = stmt.flp;
    const Expr& slice = *stmt.target;
    const bool ascendingSlice = slice.text != "downto";
    AstNodeExpr* const basep = lowerDesignator(*slice.a, true);
    if (!basep) return nullptr;
    VType baseType;
    if (!typeOfDesignator(*slice.a, baseType)) baseType = typeOfDType(basep->dtypep());
    if (!baseType.isVector() || baseType.elemWidth <= 0) {
        unsupportedError(flp, "slice of a non-array value with a run-time width");
        basep->deleteTree();
        return nullptr;
    }
    if (ascendingSlice != baseType.ascending) {
        unsupportedError(flp, "slice against the array direction with non-static bounds");
        basep->deleteTree();
        return nullptr;
    }
    const int elemWidth = baseType.elemWidth;
    const int maxCount = baseType.length();
    if (maxCount <= 0) {
        basep->deleteTree();
        return nullptr;
    }
    AstNodeExpr* const rightp = lowerExpr(*slice.c, nullptr);
    AstNodeExpr* const leftp = lowerExpr(*slice.b, nullptr);
    if (!rightp || !leftp) {
        basep->deleteTree();
        if (rightp) rightp->deleteTree();
        if (leftp) leftp->deleteTree();
        return nullptr;
    }
    // The value is evaluated once and zero-extended, so every guarded write can take its
    // own part of it (VHDL requires it to be exactly as wide as the slice)
    VType wideType = vectorType(flp, baseType.kwd, baseType.signing, maxCount * elemWidth);
    AstNodeExpr* valuep = lowerExpr(*stmt.value, &wideType);
    if (!valuep) {
        basep->deleteTree();
        rightp->deleteTree();
        leftp->deleteTree();
        return nullptr;
    }
    valuep = resizeExpr(valuep, wideType, flp);
    // The element count, captured once so every guard sees the same value
    VType intT = intType(flp);
    AstNodeExpr* countDiffp
        = new AstSub{flp, ascendingSlice ? rightp->cloneTree(false) : leftp->cloneTree(false),
                     ascendingSlice ? leftp->cloneTree(false) : rightp->cloneTree(false)};
    countDiffp->dtypep(intT.dtp);
    AstNodeExpr* const countp = new AstAdd{flp, countDiffp, makeIntConst(flp, 1)};
    countp->dtypep(intT.dtp);
    AstVar* const countVarp
        = new AstVar{flp, VVarType::VAR, "vhdlslice_" + std::to_string(++m_loopSeq), intT.dtp};
    m_modp->addStmtsp(countVarp);
    AstNodeStmt* headp = new AstAssign{flp, new AstVarRef{flp, countVarp, VAccess::WRITE}, countp};
    // The right operand is the selection's least significant element
    AstNodeExpr* offsetp = rightp->cloneTree(false);
    if (baseType.ascending) {
        offsetp = new AstSub{flp, makeIntConst(flp, baseType.right), offsetp};
    } else if (baseType.right != 0) {
        offsetp = new AstSub{flp, offsetp, makeIntConst(flp, baseType.right)};
    }
    offsetp->dtypep(intT.dtp);
    for (int i = 0; i < maxCount; ++i) {
        AstNodeExpr* const condp
            = new AstGt{flp, new AstVarRef{flp, countVarp, VAccess::READ}, makeIntConst(flp, i)};
        condp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        AstNodeExpr* const offsetAddp
            = new AstAdd{flp, offsetp->cloneTree(false), makeIntConst(flp, i * elemWidth)};
        offsetAddp->dtypep(intT.dtp);
        AstSel* const lhsp = new AstSel{flp, basep->cloneTree(false), offsetAddp, elemWidth};
        lhsp->dtypep(findVectorWidth(flp, baseType.kwd, baseType.signing, elemWidth));
        AstSel* const rhsp = new AstSel{flp, valuep->cloneTree(false),
                                        makeIntConst(flp, i * elemWidth), elemWidth};
        rhsp->dtypep(findVectorWidth(flp, baseType.kwd, baseType.signing, elemWidth));
        AstNodeStmt* const assignp
            = variable ? static_cast<AstNodeStmt*>(new AstAssign{flp, lhsp, rhsp})
                       : static_cast<AstNodeStmt*>(new AstAssignDly{flp, lhsp, rhsp});
        headp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(
            headp, new AstIf{flp, condp, assignp, nullptr});
    }
    basep->deleteTree();
    rightp->deleteTree();
    leftp->deleteTree();
    valuep->deleteTree();
    return headp;
}

AstNodeExpr* Lowering::lowerCond(const Expr& expr) {
    FileLine* const flp = expr.flp;
    // A condition that is static folds to a constant, so the body of a pure function
    // used as an elaboration-time check is never lowered
    bool condValue = false;
    if (evalCond(expr, condValue)) {
        return makeVectorConst(flp, logicType(flp), condValue ? 1 : 0);
    }
    VType boolT = logicType(flp);
    boolT.typeName = "boolean";
    // The classic `clk'event and clk = '1'` idiom is a clock edge
    if (expr.kind == Expr::BINARY && expr.text == "and" && expr.a && expr.b) {
        for (int side = 0; side < 2; ++side) {
            const Expr& attrExpr = side == 0 ? *expr.a : *expr.b;
            const Expr& levelExpr = side == 0 ? *expr.b : *expr.a;
            if (attrExpr.kind != Expr::ATTR || attrExpr.text != "event" || !attrExpr.a
                || attrExpr.a->kind != Expr::NAME) {
                continue;
            }
            if (levelExpr.kind != Expr::BINARY || (levelExpr.text != "=" && levelExpr.text != "/=")
                || !levelExpr.a || !levelExpr.b || levelExpr.a->kind != Expr::NAME
                || levelExpr.a->text != attrExpr.a->text) {
                continue;
            }
            // The level operand must be statically '0' or '1'
            const VType bitT = logicType(flp);
            AstNodeExpr* const levelConstp = lowerExpr(*levelExpr.b, &bitT);
            if (!levelConstp) continue;
            const AstConst* const constp = VN_CAST(levelConstp, Const);
            char level = 0;
            if (constp && constp->num().width() == 1) {
                if (constp->num().bitIs1(0))
                    level = '1';
                else if (constp->num().bitIs0(0))
                    level = '0';
            }
            const char opChar = levelExpr.text == "=" ? '=' : '/';
            levelConstp->deleteTree();
            if (level == 0 || opChar == 0) continue;
            if (opChar == '/') level = level == '1' ? '0' : '1';  // x /= v with a static v
            AstVar* const varp = lookupVar(*attrExpr.a);
            if (!varp) {
                flp->v3error("VHDL: unknown signal '" + attrExpr.a->spelling + "' in 'event");
                return nullptr;
            }
            Edge edge;
            edge.varp = varp;
            edge.type = level == '1' ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
            m_edges.push_back(edge);
            if (m_initial) return bitConst(flp, '0');
            if (m_edgeAsLevel) return edgeLevel(flp, varp, level == '1');
            if (m_edgeAsPrev) return edgeChanged(flp, varp);
            return bitConst(flp, '1');
        }
    }
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        return lowerEdgeCall(expr);
    }
    if (containsEdgeCall(expr)) {
        // A compound wait condition may test a level and one clock edge together
        AstNodeExpr* const compoundp = lowerExpr(expr, &boolT);
        if (!compoundp) return nullptr;
        if (compoundp->width() != 1) {
            flp->v3error("VHDL: condition is not a 1-bit value");
            compoundp->deleteTree();
            return nullptr;
        }
        return compoundp;
    }
    AstNodeExpr* condp = lowerExpr(expr, &boolT);
    if (!condp) return nullptr;
    if (condp->width() != 1) {
        flp->v3error("VHDL: condition is not a 1-bit value");
        condp->deleteTree();
        return nullptr;
    }
    return condp;
}

//! Type of a prefix name, or null when the name is not a signal or constant
//! Type of a designator (name, selected name, index or slice) when it is known
bool Lowering::typeOfDesignator(const Expr& expr, VType& out) const {
    switch (expr.kind) {
    case Expr::NAME: {
        const VType* const typep = typeOfName(expr);
        if (typep) {
            out = *typep;
            return true;
        }
        // An alias has the bounds of its mark, else the type of its designator
        const auto aliasIt = m_aliases.find(expr.text);
        if (aliasIt == m_aliases.end()) return false;
        if (aliasIt->second.hasType) {
            out = aliasIt->second.type;
            return true;
        }
        if (!m_aliasBusy.insert(expr.text).second) return false;
        const bool ok = typeOfDesignator(*aliasIt->second.designatorp, out);
        m_aliasBusy.erase(expr.text);
        return ok;
    }
    case Expr::SELECTED: {
        VType prefixType;
        if (!expr.a || !typeOfDesignator(*expr.a, prefixType)) return false;
        if (prefixType.kind != VType::RECORD) return false;
        for (const VField& field : prefixType.fields) {
            if (field.name != expr.text) continue;
            out = *field.type;
            return true;
        }
        return false;
    }
    case Expr::INDEX:
    case Expr::SLICE: {
        VType prefixType;
        if (!expr.a || !typeOfDesignator(*expr.a, prefixType)) return false;
        if (!prefixType.isVector()) return false;
        // A slice comes from an attribute range or from a subtype mark: `a'range`,
        // `a(a'range)`, `a(some_range_t)`
        const Expr* rangep = nullptr;
        if (expr.kind == Expr::INDEX && expr.args.size() == 1 && expr.args[0]->kind == Expr::ATTR
            && expr.args[0]->text == "range") {
            rangep = expr.args[0].get();
        } else if (expr.kind == Expr::SLICE && expr.b && expr.b->kind == Expr::ATTR
                   && expr.b->text == "range") {
            rangep = expr.b.get();
        }
        const VType* const markp = expr.kind == Expr::INDEX && expr.args.size() == 1
                                       ? discreteRangeType(*expr.args[0])
                                       : nullptr;
        if (expr.kind == Expr::INDEX && !rangep && !markp) {
            // One level per index: `m(i, j)` of a two-dimensional array names the
            // element, not the inner array
            VType current = prefixType;
            for (const ExprP& argp : expr.args) {
                if (!argp || argp->kind == Expr::SLICE
                    || (argp->kind == Expr::ATTR
                        && (argp->text == "range" || argp->text == "reverse_range"))
                    || discreteRangeType(*argp)) {
                    return false;  // A sliced dimension keeps its structure in the selection
                }
                if (!current.isVector()) return false;
                if (current.kind == VType::ARRAY && current.elemp) {
                    current = *current.elemp;
                } else {
                    VType elem;
                    elem.kind = current.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
                    elem.dtp = findBasic(expr.flp, current.kwd);
                    current = elem;
                }
            }
            out = current;
            return true;
        }
        // A slice keeps the element type over the sliced index range
        int64_t left = 0;
        int64_t right = 0;
        bool ascending = expr.text == "to";
        if (rangep) {
            if (!rangep->a || rangep->a->kind != Expr::NAME) return false;
            const VType* const prefixTypep = typeOfName(*rangep->a);
            if (!prefixTypep || !prefixTypep->isVector()) return false;
            left = prefixTypep->left;
            right = prefixTypep->right;
            ascending = prefixTypep->ascending;
        } else if (markp) {
            left = markp->left;
            right = markp->right;
            ascending = markp->ascending;
        } else {
            if (!expr.b || !expr.c) return false;
            if (!staticInt(expr.b, left) || !staticInt(expr.c, right)) return false;
        }
        out = prefixType;
        out.left = static_cast<int>(left);
        out.right = static_cast<int>(right);
        out.ascending = ascending;
        out.width = out.elemWidth * (std::abs(out.left - out.right) + 1);
        out.dtp = findVectorWidth(expr.flp, out.kwd, out.signing, out.width);
        return true;
    }
    default: return false;
    }
}

//! Type of an assignment target: the declared type keeps record and array structure
VType Lowering::typeOfTarget(const Expr& target, AstNodeExpr* lhsp) const {
    VType type;
    if (typeOfDesignator(target, type)) return type;
    return typeOfDType(lhsp->dtypep());
}

const VType* Lowering::typeOfName(const Expr& expr) const {
    if (expr.kind != Expr::NAME) return nullptr;
    const auto bindIt = activeEnv().find(expr.text);
    if (bindIt != activeEnv().end()) {
        if (!bindIt->second.type.dtp) return nullptr;
        return &bindIt->second.type;
    }
    const auto it = m_scope.find(expr.text);
    if (it != m_scope.end()) return &it->second.type;
    const auto constIt = m_constTypes.find(expr.text);
    if (constIt != m_constTypes.end()) return &constIt->second;
    // A type mark names no object, but `subtype'range` takes its bounds
    const auto typeIt = m_types.find(expr.text);
    if (typeIt != m_types.end()) return &typeIt->second;
    return nullptr;
}

//! Bounds a name denotes when it is used as a discrete range rather than a value, as in
//! `v(some_range_t)`. Null for an object name, so an ordinary index still resolves.
const VType* Lowering::discreteRangeType(const Expr& expr) const {
    if (expr.kind != Expr::NAME) return nullptr;
    const auto typeIt = m_types.find(expr.text);
    if (typeIt == m_types.end() || !typeIt->second.isInt()) return nullptr;
    return &typeIt->second;
}

AstNodeExpr* Lowering::lowerAttr(const Expr& expr) {
    FileLine* const flp = expr.flp;
    const std::string& attribute = expr.text;
    // `X(i)'event` is a transaction on one element of a signal. The whole-signal event
    // shortcut would miss a bit that did not change, so compare the element with its
    // previous value; sensitivity still comes from registering the base signal as an edge.
    if (attribute == "event" && expr.a
        && (expr.a->kind == Expr::INDEX || expr.a->kind == Expr::SLICE)) {
        const Expr* const basep = expr.a->a.get();
        AstVar* const varp = basep && basep->kind == Expr::NAME ? lookupVar(*basep) : nullptr;
        if (varp) {
            m_edges.push_back({varp, VEdgeType::ET_CHANGED});
            if (m_initial) return bitConst(flp, '0');
            AstNodeExpr* const curp = lowerDesignator(*expr.a, false);
            if (!curp) return nullptr;
            AstVar*& prevp = m_edgePrevVars[varp];
            if (!prevp) {
                const std::string name
                    = "vhdledge_" + std::to_string(++m_edgeSeq) + "__" + varp->name();
                prevp = new AstVar{flp, VVarType::VAR, name, varp->dtypep()};
                m_modp->addStmtsp(prevp);
            }
            AstNodeExpr* const prevExprp = curp->cloneTree(false);
            prevExprp->foreach([&](AstVarRef* const refp) {  //
                if (refp->varp() == varp) refp->varp(prevp);
            });
            AstNodeExpr* const changedp = new AstNeq{flp, curp, prevExprp};
            changedp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            return changedp;
        }
    }
    const VType* typep = nullptr;
    bool typeMark = false;
    VType resolved;
    if (!expr.a) {
        unsupportedError(flp, "attribute of a non-name prefix");
        return nullptr;
    }
    if (expr.a->kind != Expr::NAME) {
        // A designator prefix such as a record element has no name to look up, but its
        // declared type fixes every static attribute, so resolve the type instead
        if (!typeOfDesignator(*expr.a, resolved)) {
            unsupportedError(flp, "attribute of a non-name prefix");
            return nullptr;
        }
        typep = &resolved;
    }
    if (attribute == "event") {
        // `X'event` is true when X had a transaction this cycle, i.e. its value differs
        // from the previous evaluation. Register X as a sensitivity edge so the enclosing
        // process re-runs on it, then reduce like `lowerEdgeCall`: no event at time zero,
        // the level under a single-sensitivity always, else compare with the previous value
        AstVar* const varp = lookupVar(*expr.a);
        if (!varp) {
            unsupportedError(flp, "attribute 'event' on a non-signal prefix");
            return nullptr;
        }
        m_edges.push_back({varp, VEdgeType::ET_CHANGED});
        if (m_initial) return bitConst(flp, '0');
        if (m_edgeAsLevel) return bitConst(flp, '1');
        if (m_edgeAsPrev) return edgeChanged(flp, varp);
        return bitConst(flp, '1');
    }
    if (attribute == "stable" || attribute == "active" || attribute == "last_event"
        || attribute == "last_active") {
        unsupportedError(flp, "signal attribute '" + attribute + "'");
        return nullptr;
    }
    // Otherwise the prefix is either a declared name or a type mark
    if (!typep) typep = typeOfName(*expr.a);
    if (!typep) {
        const auto typeIt = m_types.find(expr.a->text);
        if (typeIt != m_types.end()) {
            typep = &typeIt->second;
            typeMark = true;
        }
    }
    VType predefined;
    if (!typep && predefinedIntegerType(flp, expr.a->text, predefined)) {
        typep = &predefined;
        typeMark = true;
    }
    if (!typep
        && (expr.a->text == "character" || expr.a->text == "boolean" || expr.a->text == "bit"
            || expr.a->text == "std_logic" || expr.a->text == "std_ulogic")) {
        // These predefined types are modelled directly rather than as entries in m_types;
        // resolve them so their attributes reach below
        TypeSpec spec;
        spec.flp = flp;
        spec.mark = expr.a->text;
        if (resolveType(spec, resolved)) {
            typep = &resolved;
            typeMark = true;
        }
    }
    // IEEE 1076-2008 16.2.2 gives every scalar type the discrete attribute set; the
    // predefined logic marks are one-bit scalars whose '0' is position 0
    if (typeMark
        && (expr.a->text == "bit" || expr.a->text == "std_logic"
            || expr.a->text == "std_ulogic")) {
        if (attribute == "left" || attribute == "low") return bitConst(flp, '0');
        if (attribute == "right" || attribute == "high") return bitConst(flp, '1');
        if (attribute == "length") return makeIntConst(flp, 1);
        if (attribute == "ascending") return bitConst(flp, '1');
        if (expr.args.size() != 1) {
            unsupportedError(flp, "attribute '" + attribute + "' without one argument");
            return nullptr;
        }
        AstNodeExpr* const argp = lowerExpr(*expr.args[0], typep);
        if (!argp) return nullptr;
        if (attribute == "pos") return resizeExpr(argp, intType(flp), flp);
        if (attribute == "val") {
            const AstConst* const argConstp = VN_CAST(argp, Const);
            if (argConstp) {
                const int64_t position = static_cast<int64_t>(argConstp->num().toUInt());
                argp->deleteTree();
                if (position != 0 && position != 1) {
                    flp->v3error("VHDL: 'val outside the range of the type");
                    return nullptr;
                }
                return bitConst(flp, position == 0 ? '0' : '1');
            }
            return resizeExpr(argp, *typep, flp);
        }
        if (attribute == "succ" || attribute == "pred") {
            unsupportedError(flp, "attribute '" + attribute + "' of a logic mark");
            return nullptr;
        }
        unsupportedError(flp, "attribute '" + attribute + "' of a logic mark");
        return nullptr;
    }
    if (!typep) {
        unsupportedError(flp, "attribute '" + attribute + "' of an unresolved name");
        return nullptr;
    }
    // Discrete-type attributes of the predefined `character` type. It is an 8-bit
    // unsigned code, so a value's position and code coincide; succ/pred step the
    // code and wrap at 255 back to 0.
    if (typeMark && expr.a->text == "character") {
        if (attribute == "pos" || attribute == "val" || attribute == "succ"
            || attribute == "pred") {
            if (expr.args.size() != 1) {
                unsupportedError(flp,
                                 "character attribute '" + attribute + "' without one argument");
                return nullptr;
            }
            AstNodeExpr* const argp = lowerExpr(*expr.args[0], typep);
            if (!argp) return nullptr;
            if (attribute == "pos") return resizeExpr(argp, intType(flp), flp);
            if (attribute == "val") return resizeExpr(argp, *typep, flp);
            AstNodeExpr* const onep = makeVectorConst(flp, *typep, 1);
            AstNodeExpr* const basep = resizeExpr(argp, *typep, flp);
            AstNodeExpr* const resultp
                = attribute == "succ" ? static_cast<AstNodeExpr*>(new AstAdd{flp, basep, onep})
                                      : static_cast<AstNodeExpr*>(new AstSub{flp, basep, onep});
            resultp->dtypep(typep->dtp);
            return resultp;
        }
        if (attribute == "left" || attribute == "low") return makeIntConst(flp, 0);
        if (attribute == "right" || attribute == "high") return makeIntConst(flp, 255);
        if (attribute == "length") return makeIntConst(flp, 256);
        if (attribute == "ascending") return bitConst(flp, '1');
        unsupportedError(flp, "character attribute '" + attribute + "'");
        return nullptr;
    }
    if (typep->kind == VType::ENUM) {
        const int count = static_cast<int>(typep->literals.size());
        AstNodeDType* const enumDtp = typep->dtp;
        auto enumConst = [&](int position) -> AstNodeExpr* {
            AstConst* const constp
                = new AstConst{flp, V3Number{flp, typep->width, static_cast<uint32_t>(position)}};
            constp->dtypep(enumDtp);
            return constp;
        };
        if (attribute == "left" || attribute == "low") return enumConst(0);
        if (attribute == "right" || attribute == "high") return enumConst(count - 1);
        if (attribute == "length") return makeIntConst(flp, count);
        // The remaining enumeration attributes are functions of one value
        if (expr.args.size() != 1) {
            unsupportedError(flp,
                             "enumeration attribute '" + attribute + "' without one argument");
            return nullptr;
        }
        AstNodeExpr* argp = lowerExpr(*expr.args[0], nullptr);
        if (!argp) return nullptr;
        // A static argument gives a constant result, which constants may use. The source
        // is evaluated, since narrowing the lowered value to the type hides the constant.
        int64_t staticPos = 0;
        const bool isStatic = evalInt(*expr.args[0], staticPos);
        argp = resizeExpr(argp, *typep, flp);
        const bool constArg = isStatic;
        const int position = isStatic ? static_cast<int>(staticPos) : 0;
        if (attribute == "pos" || attribute == "to_integer") {
            if (constArg) {
                argp->deleteTree();
                return makeIntConst(flp, position);
            }
            return resizeExpr(argp, intType(flp), flp);
        }
        if (attribute == "val") {
            if (constArg) {
                argp->deleteTree();
                // IEEE 1076-2008 16.2.2: 'val outside the type's range is an error, not a
                // value that matches no literal
                if (position < 0 || position >= count) {
                    flp->v3error("VHDL: 'val outside the range of the type");
                    return nullptr;
                }
                return enumConst(position);
            }
            return resizeExpr(argp, *typep, flp);
        }
        if ((attribute == "succ" || attribute == "pred") && constArg) {
            argp->deleteTree();
            if ((attribute == "succ" && position == count - 1)
                || (attribute == "pred" && position == 0)) {
                flp->v3error("VHDL: '" + attribute + "' of the "
                             + (attribute == "succ" ? "last" : "first")
                             + " enumeration literal is outside the type");
                return nullptr;
            }
            const int stepped = attribute == "succ" ? position + 1 : position - 1;
            return enumConst(stepped);
        }
        if (attribute == "succ" || attribute == "pred") {
            const bool succ = attribute == "succ";
            AstNodeExpr* const atLimitp
                = new AstEq{flp, argp->cloneTree(false), enumConst(succ ? count - 1 : 0)};
            atLimitp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            if (m_modp) {
                // A dynamic argument can reach the end literal at run time, where VHDL
                // requires a bound-check failure rather than a wrap
                addRuntimeError(flp, atLimitp->cloneTree(false),
                                "VHDL: '" + attribute + "' of the " + (succ ? "last" : "first")
                                    + " enumeration literal is outside the type");
            }
            // Arithmetic on an enumeration type is not something the width pass accepts, so
            // step the position in the type's vector form and cast the result back
            AstNodeDType* const bitsDtp
                = findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, typep->width);
            AstNodeExpr* const argBitsp = new AstCast{flp, argp, bitsDtp};
            AstNodeExpr* const oneBitsp = new AstCast{flp, enumConst(1), bitsDtp};
            AstNodeExpr* const steppedp
                = succ ? static_cast<AstNodeExpr*>(new AstAdd{flp, argBitsp, oneBitsp})
                       : static_cast<AstNodeExpr*>(new AstSub{flp, argBitsp, oneBitsp});
            steppedp->dtypep(bitsDtp);
            AstNodeExpr* const steppedEnump = new AstCast{flp, steppedp, enumDtp};
            steppedEnump->dtypep(enumDtp);
            AstCond* const resultp
                = new AstCond{flp, atLimitp, enumConst(succ ? 0 : count - 1), steppedEnump};
            resultp->dtypep(enumDtp);
            return resultp;
        }
        unsupportedError(flp, "enumeration attribute '" + attribute + "'");
        return nullptr;
    }
    const bool indexRange = isIndexRangeAttribute(attribute);
    if (indexRange) {
        // The bracketed index selects one index range of the prefix, not a value argument
        const VType* const levelp = indexRangeLevel(expr, *typep);
        if (!levelp) {
            unsupportedError(flp, "index of attribute '" + attribute + "'");
            return nullptr;
        }
        typep = levelp;
    }
    if (!expr.args.empty() && !indexRange) {
        // An attribute of a type mark with an argument folds statically, so a value path can
        // use the same answer a constant gets
        int64_t folded = 0;
        if (evalInt(expr, folded)) return makeIntConst(flp, folded);
        // The value as written: the decimal of an integer, the literal of a Boolean or
        // enumeration value, IEEE 1076-2008 16.2.2
        if (attribute == "image" && expr.args[0]) {
            std::string text;
            bool foldedAll = false;
            if (expr.a->text == "boolean") {  // Resolves as a 1-bit logic type
                bool condValue = false;
                foldedAll = evalCond(*expr.args[0], condValue);
                text = condValue ? "true" : "false";
            } else if (evalInt(*expr.args[0], folded)) {
                foldedAll = true;
                if (typep->kind == VType::ENUM && folded >= 0
                    && static_cast<size_t>(folded) < typep->literals.size()) {
                    text = typep->literals[static_cast<size_t>(folded)];
                } else {
                    text = std::to_string(folded);
                }
            }
            if (foldedAll) return makeStringConst(flp, text);
        }
        unsupportedError(flp, "attribute '" + attribute + "' with arguments");
        return nullptr;
    }
    int64_t value = 0;
    if (attribute == "low") {
        value = std::min(typep->left, typep->right);
    } else if (attribute == "high") {
        value = std::max(typep->left, typep->right);
    } else if (attribute == "left") {
        value = typep->left;
    } else if (attribute == "right") {
        value = typep->right;
    } else if (attribute == "length") {
        if (!typep->isVector()) {
            flp->v3error("VHDL: 'length is only defined for an array type");
            return nullptr;
        }
        value = typep->length();
    } else if (attribute == "ascending") {
        return bitConst(flp, typep->ascending ? '1' : '0');
    } else {
        (void)typeMark;
        unsupportedError(flp, "attribute '" + attribute + "'");
        return nullptr;
    }
    return makeIntConst(flp, value);
}

//! Slice elements hi..lo (inclusive, hi >= lo) out of an array-typed value
AstNodeExpr* Lowering::sliceOf(FileLine* flp, AstNodeExpr* basep, const VType& baseType,
                               int elemWidth, int hi, int lo, bool ascending) {
    // Element offsets from the least significant end; the higher index is not
    // necessarily the more significant element for an ascending array
    const int offsetHi = baseType.indexOffset(hi);
    const int offsetLo = baseType.indexOffset(lo);
    const int offset = std::min(offsetHi, offsetLo) * elemWidth;
    const int count = hi - lo + 1;
    const int width = count * elemWidth;
    if (offset < 0 || offset + width > baseType.width) {
        flp->v3error("VHDL: slice is out of range of the array");
        basep->deleteTree();
        return nullptr;
    }
    AstNodeExpr* slicep = new AstSel{flp, basep, offset, width};
    VType sliceType;
    if (baseType.kind == VType::ARRAY) {
        sliceType = baseType;
        sliceType.left = hi;
        sliceType.right = lo;
        sliceType.ascending = false;
        sliceType.width = width;
        sliceType.dtp = findVectorWidth(flp, baseType.kwd, baseType.signing, width);
    } else {
        sliceType = vectorType(flp, baseType.kwd, baseType.signing, width);
    }
    slicep->dtypep(sliceType.dtp);
    if (ascending != baseType.ascending) {
        // The slice's left bound is its most significant element. When its
        // direction differs from the base's, the selected elements are reversed.
        AstNodeDType* const elemDtp = baseType.kind == VType::ARRAY && baseType.elemp
                                          ? baseType.elemp->dtp
                                          : findBasic(flp, baseType.kwd);
        AstNodeExpr* reversedp = nullptr;
        for (int unit = 0; unit < count; ++unit) {
            AstNodeExpr* const unitp = new AstSel{flp, slicep->cloneTree(false),
                                                  (count - 1 - unit) * elemWidth, elemWidth};
            unitp->dtypep(elemDtp);
            reversedp = reversedp ? static_cast<AstNodeExpr*>(new AstConcat{flp, unitp, reversedp})
                                  : unitp;
        }
        slicep->deleteTree();
        reversedp->dtypep(sliceType.dtp);
        return reversedp;
    }
    return slicep;
}

//! An alias takes the index bounds its mark declares; the bits come from the designator.
//! An alias with a signature renames a subprogram instead, so it is registered as one.
void Lowering::registerAlias(const AliasDecl& alias) {
    if (!alias.designator) return;
    if (alias.hasSignature) {
        registerSubprogramAlias(alias);
        return;
    }
    AliasInfo info;
    info.designatorp = alias.designator.get();
    if (alias.mark.flp && resolveType(alias.mark, info.type)) info.hasType = true;
    m_aliases[alias.name] = info;
}

//! `alias name is pkg.designator[signature];` is a synonym: it takes the designator's
//! declarations, so calls of the alias resolve like calls of the designator. All its
//! overloads come along: picking the one the signature names would reject the legal alias
//! that writes a subtype's name for the same type, and the call site already resolves the
//! overload from the actuals.
void Lowering::registerSubprogramAlias(const AliasDecl& alias) {
    const Expr& designator = *alias.designator;
    if (designator.kind == Expr::SELECTED && designator.a) {
        std::string pkg;
        if (packageNameOf(*designator.a, pkg)) importPackage(pkg);
    }
    const auto it = m_subprograms.find(designator.text);
    if (it == m_subprograms.end()) return;  // A standard-package name has no declaration
    std::vector<SubprogramRef>& refs = m_subprograms[alias.name];
    if (&refs == &it->second) return;  // An alias may not rename its own designator
    for (const SubprogramRef& ref : it->second) {
        const bool known = std::any_of(refs.begin(), refs.end(), [=](const SubprogramRef& r) {
            return r.declp == ref.declp;
        });
        if (known) continue;
        refs.push_back(ref);
        m_imported.insert(alias.name);
    }
}

//! Lower a use of an alias by lowering the designator it renames
AstNodeExpr* Lowering::lowerAlias(const Expr& nameExpr, bool write) {
    const auto it = m_aliases.find(nameExpr.text);
    if (it == m_aliases.end()) return nullptr;
    if (!m_aliasBusy.insert(nameExpr.text).second) {
        nameExpr.flp->v3error("VHDL: alias '" + nameExpr.spelling + "' refers to itself");
        return nullptr;
    }
    AstNodeExpr* resultp = lowerDesignator(*it->second.designatorp, write);
    m_aliasBusy.erase(nameExpr.text);
    if (resultp && it->second.hasType && it->second.type.dtp) {
        resultp->dtypep(it->second.type.dtp);
    }
    return resultp;
}

//! Package name when a prefix denotes one, e.g. `pkg` or `work.pkg`
bool Lowering::packageNameOf(const Expr& prefix, std::string& name) const {
    if (!m_packagesp) return false;
    const auto isPackage = [&](const std::string& candidate) {
        return m_packagesp->packages.count(candidate) != 0;
    };
    if (prefix.kind == Expr::NAME) {
        if (!isPackage(prefix.text)) return false;
        name = prefix.text;
        return true;
    }
    if (prefix.kind == Expr::SELECTED && prefix.a && prefix.a->kind == Expr::NAME
        && isPackage(prefix.text)) {
        // Library-qualified package name: the library is not modelled
        name = prefix.text;
        return true;
    }
    return false;
}

//! A name declared by a package: import the package, then resolve in its tables
AstNodeExpr* Lowering::lowerPackageMember(const Expr& expr, const std::string& pkgName) {
    FileLine* const flp = expr.flp;
    importPackage(pkgName);
    const auto constIt = m_consts.find(expr.text);
    if (constIt != m_consts.end()) return makeIntConst(flp, constIt->second);
    const auto valueIt = m_constValues.find(expr.text);
    if (valueIt != m_constValues.end()) return valueIt->second->cloneTree(false);
    const auto enumIt = m_enumConsts.find(expr.text);
    if (enumIt != m_enumConsts.end()) return enumIt->second->cloneTree(false);
    flp->v3error("VHDL: package '" + pkgName + "' has no declaration '" + expr.spelling + "'");
    return nullptr;
}

AstNodeExpr* Lowering::lowerSelectedExpr(const Expr& expr) {
    FileLine* const flp = expr.flp;
    if (!expr.a) {
        unsupportedError(flp, "selected name without a prefix");
        return nullptr;
    }
    std::string pkgName;
    if (packageNameOf(*expr.a, pkgName)) return lowerPackageMember(expr, pkgName);
    VType prefixType;
    if (!typeOfDesignator(*expr.a, prefixType) || prefixType.kind != VType::RECORD) {
        unsupportedError(flp, "selected name '" + expr.spelling + "'");
        return nullptr;
    }
    for (const VField& field : prefixType.fields) {
        if (field.name != expr.text) continue;
        AstNodeExpr* const basep = lowerExpr(*expr.a, nullptr);
        if (!basep) return nullptr;
        AstSel* const selp = new AstSel{flp, basep, field.offset, field.width};
        selp->dtypep(field.type->dtp);
        return selp;
    }
    flp->v3error("VHDL: record has no element '" + expr.spelling + "'");
    return nullptr;
}

//! Value of a lowered expression that has no run-time input, or null when one operand is
//! not known. The expression is left as it is, so a caller that needs a value keeps the
//! run-time form. This is the frontend's own fold: VHDL is lowered while
//! V3Global::readFiles holds the user4 slot V3Const claims, and a nested claim aborts.
AstConst* Lowering::foldStatic(AstNodeExpr* exprp) const {
    if (!exprp) return nullptr;
    const auto result = [&](const V3Number& num) {
        AstConst* const constp = new AstConst{exprp->fileline(), num};
        constp->dtypep(exprp->dtypep());
        return constp;
    };
    if (AstConst* const constp = VN_CAST(exprp, Const)) { return constp->cloneTree(false); }
    if (exprp->dtypep() && exprp->dtypep()->isDouble()) return nullptr;
    V3Number lhs{exprp, 1};
    V3Number rhs{exprp, 1};
    const auto operand = [&](AstNodeExpr* operandp, V3Number& out) {
        AstConst* const constp = foldStatic(operandp);
        if (!constp) return false;
        out = constp->num();
        constp->deleteTree();
        return true;
    };
    if (AstNodeUniop* const nodep = VN_CAST(exprp, NodeUniop)) {
        if (!operand(nodep->lhsp(), lhs) || lhs.isDouble()) return nullptr;
        V3Number num{nodep, nodep->width()};
        nodep->numberOperate(num, lhs);
        return result(num);
    }
    if (AstNodeBiop* const nodep = VN_CAST(exprp, NodeBiop)) {
        if (!operand(nodep->lhsp(), lhs) || !operand(nodep->rhsp(), rhs)) return nullptr;
        if (lhs.isDouble() || rhs.isDouble()) return nullptr;
        V3Number num{nodep, nodep->width()};
        nodep->numberOperate(num, lhs, rhs);
        return result(num);
    }
    if (AstCond* const nodep = VN_CAST(exprp, Cond)) {
        // A condition that is known picks the arm, as the run-time form would
        AstConst* const testp = foldStatic(nodep->condp());
        if (!testp) return nullptr;
        const bool taken = testp->num().isTrue();
        testp->deleteTree();
        return foldStatic(taken ? nodep->thenp() : nodep->elsep());
    }
    if (AstSel* const nodep = VN_CAST(exprp, Sel)) {
        AstConst* const basep = foldStatic(nodep->fromp());
        AstConst* const lsbp = foldStatic(nodep->lsbp());
        if (!basep || !lsbp) {
            if (basep) basep->deleteTree();
            if (lsbp) lsbp->deleteTree();
            return nullptr;
        }
        const int lsb = lsbp->num().toSInt();
        lsbp->deleteTree();
        V3Number num{nodep, nodep->width()};
        num.opSel(basep->num(), lsb + nodep->widthConst() - 1, lsb);
        basep->deleteTree();
        return result(num);
    }
    return nullptr;
}
//! Type of an aggregate with no target type: the element comes from its values and the bounds
//! from its choices. Only a bit vector can be derived, since a wider element would need the
//! array type its element names, which an operand position does not give
bool Lowering::deriveAggregateType(const Expr& expr, VType& out) {
    FileLine* const flp = expr.flp;
    const auto refuse = [flp] {
        unsupportedError(flp, "aggregate without a known target type");
        return false;
    };
    if (expr.args.empty() || expr.kind == Expr::AGG_OTHERS) return refuse();
    AstNodeExpr* const valuep = lowerExpr(*expr.args.front(), nullptr);
    if (!valuep) return false;  // The value reported its own error
    const VType elem = typeOfDType(valuep->dtypep());
    valuep->deleteTree();
    if (elem.kind != VType::LOGIC && elem.kind != VType::BIT) return refuse();
    int64_t lo = 0;
    int64_t hi = -1;
    if (expr.kind == Expr::AGG_POSITIONAL) {
        // Each positional association covers one element, or the bits of one literal
        int count = 0;
        for (const ExprP& argp : expr.args) {
            count += (argp && argp->kind == Expr::BITS_LIT) ? static_cast<int>(argp->bits.size())
                                                            : 1;
        }
        hi = count - 1;
    } else {
        for (size_t i = 0; i < expr.args.size(); ++i) {
            for (const ExprP& choicep : expr.choices[i]) {
                int64_t low = 0;
                int64_t high = 0;
                if (choicep->kind == Expr::SLICE) {
                    // The bounds may be folded calls, e.g. `(log2(SIZE) - 1 downto 0 => '0')`
                    if (!staticIntFold(choicep->b, low) || !staticIntFold(choicep->c, high)) {
                        return refuse();
                    }
                    // A choice names its bounds in its own direction: `7 downto 4` covers
                    // the same elements as `4 to 7`, so only the span fixes the width
                    if (low > high) std::swap(low, high);
                } else if (choicep->kind == Expr::ATTR
                           && (choicep->text == "range" || choicep->text == "reverse_range")
                           && choicep->a) {
                    // The range of an object, e.g. `a'range`: its bounds come from the
                    // prefix's own type, just as they do for a lowered choice
                    VType rangeType;
                    if (!typeOfDesignator(*choicep->a, rangeType) || !rangeType.isVector()) {
                        return refuse();
                    }
                    low = std::min(rangeType.left, rangeType.right);
                    high = std::max(rangeType.left, rangeType.right);
                } else if (choicep->kind == Expr::NAME && choicep->text == "others") {
                    return refuse();
                } else if (!staticIntFold(choicep, high)) {
                    return refuse();
                } else {
                    low = high;
                }
                if (hi < 0) {
                    lo = low;
                    hi = high;
                } else {
                    lo = std::min(lo, low);
                    hi = std::max(hi, high);
                }
            }
        }
        if (hi < 0) return refuse();
    }
    out = vectorType(flp, elem.kwd, VSigning::UNSIGNED, static_cast<int>(hi - lo + 1));
    out.left = static_cast<int>(lo);
    out.right = static_cast<int>(hi);
    out.ascending = true;
    out.dtp = findVector(flp, elem.kwd, VSigning::UNSIGNED, out.left, out.right, true);
    return true;
}

AstNodeExpr* Lowering::repeatValue(FileLine* flp, AstNodeExpr* valuep, int count,
                                   AstNodeDType* dtp) {
    if (count <= 1) {
        if (dtp) valuep->dtypep(dtp);
        return valuep;
    }
    if (AstConst* const constp = VN_CAST(valuep, Const)) {
        // A constant aggregate is one value, not a chain of concatenations. Copy
        // directly: V3Number::opRepl warns above --replication-limit, which does not
        // apply to an aggregate.
        V3Number num{flp, constp->width() * count, 0u};
        for (int i = 0; i < count; ++i) {
            num.opSelInto(constp->num(), i * constp->width(), constp->width());
        }
        AstConst* const resultp = new AstConst{flp, num};
        resultp->dtypep(dtp);
        valuep->deleteTree();
        return resultp;
    }
    // V3Width warns on a replication above --replication-limit, which is a heuristic
    // for hand-written RTL; an aggregate legitimately repeats many times. Chunk it,
    // and combine the chunks in a balanced tree.
    const int limit = v3Global.opt.replicationLimit();
    const int chunkSize = limit > 0 ? std::min(limit, count) : count;
    std::vector<AstNodeExpr*> parts;
    for (int remaining = count; remaining > 0;) {
        const int n = std::min(chunkSize, remaining);
        AstNodeExpr* const srcp = parts.empty() ? valuep : valuep->cloneTree(false);
        parts.push_back(new AstReplicate{flp, srcp, static_cast<uint32_t>(n)});
        remaining -= n;
    }
    while (parts.size() > 1) {
        std::vector<AstNodeExpr*> next;
        for (std::size_t i = 0; i + 1 < parts.size(); i += 2) {
            next.push_back(new AstConcat{flp, parts[i], parts[i + 1]});
        }
        if (parts.size() % 2) next.push_back(parts.back());
        parts = std::move(next);
    }
    parts.front()->dtypep(dtp);
    return parts.front();
}

AstNodeExpr* Lowering::lowerAggregate(const Expr& expr, const VType& type) {
    FileLine* const flp = expr.flp;
    if (!type.isVector() && type.kind != VType::RECORD) {
        unsupportedError(flp, "aggregate for a non-composite type");
        return nullptr;
    }
    //! One run of elements covered by one associated value
    struct Segment final {
        int offset = 0;
        int width = 1;
        const Expr* exprp = nullptr;
    };
    // Positional associations may each span several elements, e.g. `("01", "10")`
    if (expr.kind == Expr::AGG_POSITIONAL && type.isVector() && !expr.args.empty()) {
        int total = 0;
        bool allBits = true;
        for (const ExprP& argp : expr.args) {
            if (!argp || argp->kind != Expr::BITS_LIT) {
                allBits = false;
                break;
            }
            total += static_cast<int>(argp->bits.size());
        }
        if (allBits && total == type.width
            && total != static_cast<int>(expr.args.size()) * type.elemWidth) {
            AstNodeExpr* resultp = nullptr;
            for (const ExprP& argp : expr.args) {
                AstNodeExpr* const valuep = lowerExpr(*argp, nullptr);
                if (!valuep) {
                    if (resultp) resultp->deleteTree();
                    return nullptr;
                }
                resultp = resultp ? static_cast<AstNodeExpr*>(new AstConcat{flp, resultp, valuep})
                                  : valuep;
            }
            resultp->dtypep(type.dtp);
            return resultp;
        }
    }
    std::vector<Segment> segments;
    const Expr* othersValuep = nullptr;  // Value of an `others` association
    if (type.kind == VType::RECORD) {
        if (expr.kind == Expr::AGG_POSITIONAL) {
            // Positional association fills record elements in declaration order
            if (expr.args.size() != type.fields.size()) {
                unsupportedError(flp, "record aggregate that is not a full positional list");
                return nullptr;
            }
            for (size_t i = 0; i < expr.args.size(); ++i) {
                Segment segment;
                segment.offset = type.fields[i].offset;
                segment.width = type.fields[i].width;
                segment.exprp = expr.args[i].get();
                segments.push_back(segment);
            }
        } else {
            // Named association: each choice names one record element, or is `others`
            for (size_t i = 0; i < expr.args.size(); ++i) {
                for (const ExprP& choicep : expr.choices[i]) {
                    if (choicep->kind == Expr::NAME && choicep->text == "others") {
                        othersValuep = expr.args[i].get();
                        continue;
                    }
                    if (choicep->kind != Expr::NAME) {
                        unsupportedError(flp,
                                         "record aggregate choice that is not an element name");
                        return nullptr;
                    }
                    const auto fieldIt = std::find_if(
                        type.fields.begin(), type.fields.end(),
                        [&](const VField& field) { return field.name == choicep->text; });
                    if (fieldIt == type.fields.end()) {
                        flp->v3error("VHDL: record has no element '" + choicep->spelling + "'");
                        return nullptr;
                    }
                    Segment segment;
                    segment.offset = fieldIt->offset;
                    segment.width = fieldIt->width;
                    segment.exprp = expr.args[i].get();
                    segments.push_back(segment);
                }
            }
        }
    } else {
        const int elemWidth = type.elemWidth;
        for (size_t i = 0; i < expr.args.size(); ++i) {
            const std::vector<ExprP>& choiceList = expr.choices[i];
            if (choiceList.size() == 1 && choiceList.front()->kind == Expr::NAME
                && choiceList.front()->text == "others") {
                othersValuep = expr.args[i].get();
                continue;
            }
            auto addIndex = [&](int64_t index, const Expr* valuep) {
                Segment segment;
                segment.offset = type.indexOffset(static_cast<int>(index)) * elemWidth;
                segment.width = elemWidth;
                segment.exprp = valuep;
                segments.push_back(segment);
            };
            auto addRange = [&](int64_t left, int64_t right, const Expr* valuep) {
                const int offsetLeft = type.indexOffset(static_cast<int>(left));
                const int offsetRight = type.indexOffset(static_cast<int>(right));
                Segment segment;
                segment.offset = std::min(offsetLeft, offsetRight) * elemWidth;
                segment.width = (std::abs(offsetLeft - offsetRight) + 1) * elemWidth;
                segment.exprp = valuep;
                segments.push_back(segment);
            };
            if (choiceList.empty()) {
                // Positional: the next element in declaration order
                const int64_t index = type.ascending ? type.left + static_cast<int64_t>(i)
                                                     : type.left - static_cast<int64_t>(i);
                addIndex(index, expr.args[i].get());
                continue;
            }
            for (const ExprP& choicep : choiceList) {
                if (choicep->kind == Expr::SLICE) {
                    int64_t left = 0;
                    int64_t right = 0;
                    // A bound may be a folded call, e.g. `(log2(SIZE) - 1 downto 0 => '0')`
                    if (!staticIntFold(choicep->b, left) || !staticIntFold(choicep->c, right)) {
                        unsupportedError(flp, "non-static aggregate choice range");
                        return nullptr;
                    }
                    addRange(left, right, expr.args[i].get());
                    continue;
                }
                if (choicep->kind == Expr::ATTR
                    && (choicep->text == "range" || choicep->text == "reverse_range")
                    && choicep->a) {
                    // The range of an object, e.g. `a'range`
                    VType rangeType;
                    if (!typeOfDesignator(*choicep->a, rangeType) || !rangeType.isVector()) {
                        unsupportedError(flp, "non-static aggregate choice range");
                        return nullptr;
                    }
                    addRange(rangeType.left, rangeType.right, expr.args[i].get());
                    continue;
                }
                int64_t index = 0;
                if (!staticInt(choicep, index)) {
                    unsupportedError(flp, "non-static aggregate choice");
                    return nullptr;
                }
                addIndex(index, expr.args[i].get());
            }
        }
    }
    // Fill the elements no association names: a record fills whole fields, an array
    // element-sized pieces
    if (type.kind == VType::RECORD && othersValuep) {
        for (const VField& field : type.fields) {
            const bool covered
                = std::any_of(segments.begin(), segments.end(), [&](const Segment& segment) {
                      return field.offset >= segment.offset
                             && field.offset < segment.offset + segment.width;
                  });
            if (covered) continue;
            Segment segment;
            segment.offset = field.offset;
            segment.width = field.width;
            segment.exprp = othersValuep;
            segments.push_back(segment);
        }
    }
    const int elemSpan = type.kind == VType::RECORD ? 1 : std::max(1, type.elemWidth);
    const int elementCount = elemSpan > 0 ? type.width / elemSpan : 0;
    if (type.kind != VType::RECORD && !othersValuep) {
        // No `others`: every element must be covered by an association
    } else if (othersValuep && type.kind != VType::RECORD) {
        for (int element = 0; element < elementCount; ++element) {
            const int offset = element * elemSpan;
            const bool covered
                = std::any_of(segments.begin(), segments.end(), [&](const Segment& segment) {
                      return offset >= segment.offset && offset < segment.offset + segment.width;
                  });
            if (covered) continue;
            Segment segment;
            segment.offset = offset;
            segment.width = elemSpan;
            segment.exprp = othersValuep;
            segments.push_back(segment);
        }
    }
    // The association must cover the whole type exactly once
    std::sort(segments.begin(), segments.end(),
              [](const Segment& l, const Segment& r) { return l.offset < r.offset; });
    int covered = 0;
    for (const Segment& segment : segments) {
        if (segment.offset != covered) {
            unsupportedError(flp, "aggregate that does not cover every element");
            return nullptr;
        }
        covered += segment.width;
    }
    if (covered != type.width) {
        unsupportedError(flp, "aggregate that does not cover every element");
        return nullptr;
    }
    // Value of one segment, typed and widened to the segment
    const auto lowerSegment = [&](const Segment& segment) -> AstNodeExpr* {
        VType segType;
        if (segment.width == type.elemWidth && type.kind == VType::ARRAY && type.elemp) {
            segType = *type.elemp;
        } else if (type.kind == VType::RECORD) {
            const auto fieldIt
                = std::find_if(type.fields.begin(), type.fields.end(), [&](const VField& field) {
                      return field.offset == segment.offset;
                  });
            if (fieldIt != type.fields.end()) segType = *fieldIt->type;
        }
        if (segType.dtp == nullptr) {
            segType = type.kind == VType::RECORD
                          ? vectorType(flp, type.kwd, VSigning::UNSIGNED, segment.width)
                          : vectorType(flp, type.kwd, type.signing, segment.width);
        }
        AstNodeExpr* valuep = nullptr;
        if (type.isVector() && segment.width > type.elemWidth) {
            // A range association's value is either one element, repeated over the
            // whole range, or spans the range itself
            const VType elemType = elementType(type, flp);
            if (elemType.width <= 0) return nullptr;
            valuep = lowerExpr(*segment.exprp, &elemType);
            if (!valuep) return nullptr;
            if (valuep->width() == elemType.width) {
                AstNodeExpr* const elemp = valuep;
                valuep = elemp->cloneTree(false);
                for (int width = elemType.width; width < segment.width; width += elemType.width) {
                    valuep = new AstConcat{flp, elemp->cloneTree(false), valuep};
                }
                elemp->deleteTree();
            }
            if (valuep->width() != segment.width) valuep = resizeExpr(valuep, segType, flp);
        }
        if (!valuep) {
            valuep = lowerExpr(*segment.exprp, &segType);
            if (!valuep) return nullptr;
            valuep = resizeExpr(valuep, segType, flp);
        }
        return valuep;
    };
    // `(others => value)` gives every uncovered element the same value: lower it once
    // and replicate. Expanding it into one concatenation per element made a large
    // memory image quadratic in the array size.
    bool uniform = segments.size() > 1;
    for (std::size_t i = 1; uniform && i < segments.size(); ++i) {
        uniform = segments[i].exprp == segments.front().exprp
                  && segments[i].width == segments.front().width
                  && segments[i].offset == static_cast<int>(i) * segments.front().width;
    }
    if (uniform && type.isVector()) {
        AstNodeExpr* const valuep = lowerSegment(segments.front());
        if (!valuep) return nullptr;
        return repeatValue(flp, valuep, static_cast<int>(segments.size()), type.dtp);
    }
    // Assemble from the least significant segment up
    AstNodeExpr* resultp = nullptr;
    bool allConst = true;
    for (const Segment& segment : segments) {
        AstNodeExpr* valuep = lowerSegment(segment);
        if (!valuep) {
            if (resultp) resultp->deleteTree();
            return nullptr;
        }
        if (!VN_IS(valuep, Const)) allConst = false;
        resultp
            = resultp ? static_cast<AstNodeExpr*>(new AstConcat{flp, valuep, resultp}) : valuep;
    }
    if (allConst) {
        // Fold an aggregate of constants down to a single constant
        if (AstConst* const constp = foldStatic(resultp)) {
            constp->dtypep(type.dtp);
            resultp->deleteTree();
            return constp;
        }
    }
    resultp->dtypep(type.dtp);
    return resultp;
}

AstNodeExpr* Lowering::lowerMatching(const Expr& expr, AstNodeExpr* lp, AstNodeExpr* rp) {
    FileLine* const flp = expr.flp;
    const int width = lp->dtypep()->width();
    // A '-' character in a literal is a don't-care position for matching
    std::vector<bool> wildcard(static_cast<size_t>(width), false);
    bool anyWildcard = false;
    for (const Expr* const sidep : {expr.a.get(), expr.b.get()}) {
        if (!sidep || sidep->kind != Expr::BITS_LIT) continue;
        const std::string& bits = sidep->bits;
        for (size_t i = 0; i < bits.size() && static_cast<int>(i) < width; ++i) {
            if (bits[i] != '-') continue;
            const int bit = width - 1 - static_cast<int>(i);
            if (bit < 0 || bit >= width) continue;
            wildcard[static_cast<size_t>(bit)] = true;
            anyWildcard = true;
        }
    }
    // A don't-care can also arrive through a constant: the pattern operand is a
    // lowered constant whose '-' bits are x bits (IEEE 1076-2008 16.8.3)
    for (const AstNodeExpr* const sidep : {lp, rp}) {
        const AstConst* const constp = VN_CAST(sidep, Const);
        if (!constp) continue;
        for (int bit = 0; bit < width; ++bit) {
            if (!constp->num().bitIsX(bit)) continue;
            wildcard[static_cast<size_t>(bit)] = true;
            anyWildcard = true;
        }
    }
    AstNodeDType* const dtp = lp->dtypep();
    AstNodeExpr* matchp = nullptr;
    if (anyWildcard) {
        // Keep the compared positions and clear the don't-care ones
        V3Number keep{flp, width, 0u};
        bool kept = false;
        for (int bit = 0; bit < width; ++bit) {
            if (wildcard[static_cast<size_t>(bit)]) continue;
            keep.setBit(bit, '1');
            kept = true;
        }
        if (!kept) {
            // Every position is a don't-care: the pattern matches any value
            lp->deleteTree();
            rp->deleteTree();
            return bitConst(flp, '1');
        }
        AstNodeExpr* const xorp = new AstXor{flp, lp, rp};
        xorp->dtypep(dtp);
        AstConst* const keepp = new AstConst{flp, keep};
        keepp->dtypep(dtp);
        AstNodeExpr* const andp = new AstAnd{flp, xorp, keepp};
        andp->dtypep(dtp);
        AstConst* const zerop = new AstConst{flp, V3Number{flp, width, 0u}};
        zerop->dtypep(dtp);
        matchp = new AstEqCase{flp, andp, zerop};
    } else {
        // Without a wildcard literal, matching is same-value equality
        matchp = new AstEqCase{flp, lp, rp};
    }
    matchp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    return matchp;
}

//! Whether a conditional assignment is gated by a clock edge

}  // namespace vhdlnative
