// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL frontend lowering: fixed-point types
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
// Lowers the sfixed and ufixed types of IEEE 1076-2008 fixed_pkg. A fixed-point
// value is a signed or unsigned vector whose index range (left downto right) places
// the binary point: bit `i` weighs 2**i. The operators and conversions below work on
// the vector bits, aligned onto the grid the result bounds name.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNativeLowerImpl.h"

#include <algorithm>
#include <cmath>

namespace vhdlnative {

//======================================================================
// Types

VType Lowering::fixedType(FileLine* flp, bool isSigned, int left, int right) const {
    VType type = vectorType(flp, VBasicDTypeKwd::LOGIC,
                            isSigned ? VSigning::SIGNED : VSigning::UNSIGNED, left - right + 1);
    type.left = left;
    type.right = right;
    type.elemWidth = 1;
    type.typeName = isSigned ? "sfixed" : "ufixed";
    return type;
}

bool Lowering::isFixedName(const std::string& name) {
    return name == "sfixed" || name == "ufixed" || name == "u_sfixed" || name == "u_ufixed";
}

bool Lowering::fixedPackageUsed() const {
    return m_stdUses.count("fixed_pkg") || m_stdUses.count("fixed_generic_pkg");
}

AstNodeExpr* Lowering::noteFixed(const Expr& expr, AstNodeExpr* nodep, const VType& type) {
    m_exprTypes[&expr] = LoweredType{nodep, type};
    return nodep;
}

//======================================================================
// Building blocks

//! The value on the bit grid (high downto low): extended above by its sign or zeros,
//! with zeros below
AstNodeExpr* Lowering::fixedAlign(FileLine* flp, AstNodeExpr* valuep, const VType& type, int high,
                                  int low) {
    const int width = high - low + 1;
    const VType wide = vectorType(flp, VBasicDTypeKwd::LOGIC, type.signing, width);
    AstNodeExpr* resultp = resizeExpr(valuep, wide, flp);
    const int shift = type.right - low;
    if (shift > 0) {
        resultp = new AstShiftL{flp, resultp, makeIntConst(flp, shift), width};
        resultp->dtypep(wide.dtp);
    }
    return resultp;
}

//! The bits [lsb +: width] of an expression as an unsigned vector
AstNodeExpr* Lowering::fixedSel(FileLine* flp, AstNodeExpr* fromp, int lsb, int width) {
    AstSel* const selp = new AstSel{flp, fromp, lsb, width};
    selp->dtypep(width == 1
                     ? findBasic(flp, VBasicDTypeKwd::LOGIC)
                     : findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, width));
    return selp;
}

//! resize (IEEE 1076-2008 fixed_pkg): the argument rounded (half to even) onto the bits
//! `right` and above, then saturated or wrapped into `left downto right`
AstNodeExpr* Lowering::fixedResize(FileLine* flp, AstNodeExpr* argp, const VType& at, int left,
                                   int right, bool saturate, bool round) {
    const bool isSigned = at.isSigned();
    const VSigning signing = isSigned ? VSigning::SIGNED : VSigning::UNSIGNED;
    const int width = left - right + 1;
    const int argWidth = at.width;
    const int shift = right - at.right;  // Positive: low bits of the argument are dropped
    // Room for the argument scaled up, the target and the carry of a rounding increment
    const int work = std::max(argWidth + std::max(-shift, 0), width) + 2;
    const VType workType = vectorType(flp, VBasicDTypeKwd::LOGIC, signing, work);
    // Rounding only applies when the dropped bits start inside the argument
    const bool rounds = round && shift > 0 && shift <= argWidth;
    AstNodeExpr* halfp = nullptr;
    AstNodeExpr* stickyp = nullptr;
    if (rounds) {
        halfp = fixedSel(flp, argp->cloneTree(false), shift - 1, 1);
        if (shift >= 2) {
            stickyp
                = makeReduction(flp, fixedSel(flp, argp->cloneTree(false), 0, shift - 1), "or");
        }
    }
    AstNodeExpr* qp = resizeExpr(argp, workType, flp);
    if (shift < 0) {
        qp = new AstShiftL{flp, qp, makeIntConst(flp, -shift), work};
        qp->dtypep(workType.dtp);
    } else if (shift > 0) {
        AstConst* const amountp = makeIntConst(flp, std::min(shift, argWidth));
        if (isSigned) {
            qp = new AstShiftRS{flp, qp, amountp, work};
        } else {
            qp = new AstShiftR{flp, qp, amountp, work};
        }
        qp->dtypep(workType.dtp);
    }
    AstNodeDType* const bitDtp = findBasic(flp, VBasicDTypeKwd::LOGIC);
    if (rounds) {
        // Increment when the first dropped bit is set and either a lower dropped bit is
        // set or the kept least significant bit is (round half to even)
        AstNodeExpr* tiep = fixedSel(flp, qp->cloneTree(false), 0, 1);
        if (stickyp) {
            tiep = new AstOr{flp, stickyp, tiep};
            tiep->dtypep(bitDtp);
        }
        AstNodeExpr* incp = new AstAnd{flp, halfp, tiep};
        incp->dtypep(bitDtp);
        AstNodeExpr* const widep = new AstExtend{flp, incp, work};
        widep->dtypep(workType.dtp);
        qp = new AstAdd{flp, qp, widep};
        qp->dtypep(workType.dtp);
    }
    const VType resultType = fixedType(flp, isSigned, left, right);
    AstNodeExpr* const lowp = fixedSel(flp, qp->cloneTree(false), 0, width);
    lowp->dtypep(resultType.dtp);
    if (!saturate) {
        qp->deleteTree();
        return lowp;
    }
    const std::string ones(static_cast<size_t>(width), '1');
    const std::string zeros(static_cast<size_t>(width - 1), '0');
    const std::string highOnes(static_cast<size_t>(width - 1), '1');
    AstNodeExpr* condp = nullptr;
    AstNodeExpr* satp = nullptr;
    if (!isSigned) {
        // An unsigned value overflows when any bit above the target is set
        condp = new AstNot{flp, makeReduction(flp, fixedSel(flp, qp, width, work - width), "or")};
        satp = makeBitsConst(flp, resultType.dtp, ones);
    } else {
        // A signed value fits when the bits from the target's sign upward all agree
        AstNodeExpr* const topp = fixedSel(flp, qp->cloneTree(false), width - 1, work - width + 1);
        AstNodeExpr* const nonzerop = makeReduction(flp, topp->cloneTree(false), "or");
        AstNodeExpr* const allonesp = makeReduction(flp, topp, "and");
        AstNodeExpr* const notZerop = new AstNot{flp, nonzerop};
        notZerop->dtypep(bitDtp);
        condp = new AstOr{flp, notZerop, allonesp};
        // A negative overflow saturates to the most negative value
        AstCond* const signedSatp = new AstCond{
            flp, fixedSel(flp, qp, work - 1, 1), makeBitsConst(flp, resultType.dtp, "1" + zeros),
            makeBitsConst(flp, resultType.dtp, "0" + highOnes)};
        signedSatp->dtypep(resultType.dtp);
        satp = signedSatp;
    }
    condp->dtypep(bitDtp);  // True when the value fits
    AstCond* const resultp = new AstCond{flp, condp, lowp, satp};
    resultp->dtypep(resultType.dtp);
    return resultp;
}

//======================================================================
// Operators

AstNodeExpr* Lowering::lowerFixedBinary(FileLine* flp, const Expr& expr, const std::string& op,
                                        BinaryOperands& ops) {
    AstNodeExpr* lp = ops.lp;
    AstNodeExpr* rp = ops.rp;
    const auto fail = [&](const std::string& why) -> AstNodeExpr* {
        if (!why.empty()) flp->v3error(why);
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    };
    VType lt = ops.lt;
    VType rt = ops.rt;
    // A natural or integer operand takes the bounds of the fixed-point one
    // (fixed_pkg: `l + to_ufixed(r, l)`)
    if (isFixedType(lt) && rt.isInt()) {
        rp = fixedResize(flp, rp, fixedType(flp, lt.isSigned(), 31, 0), lt.left, lt.right, true,
                         true);
        rt = fixedType(flp, lt.isSigned(), lt.left, lt.right);
    } else if (isFixedType(rt) && lt.isInt()) {
        lp = fixedResize(flp, lp, fixedType(flp, rt.isSigned(), 31, 0), rt.left, rt.right, true,
                         true);
        lt = fixedType(flp, rt.isSigned(), rt.left, rt.right);
    }
    if (!isFixedType(lt) || !isFixedType(rt))
        return fail("VHDL: operands of '" + op + "' mix a fixed-point and another type.");
    if (lt.isSigned() != rt.isSigned()) {
        return fail("VHDL: operands of '" + op + "' mix sfixed and ufixed.");
    }
    const bool isSigned = lt.isSigned();
    const bool relational
        = op == "=" || op == "/=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    AstNodeDType* const bitDtp = findBasic(flp, VBasicDTypeKwd::LOGIC);
    if (relational) {
        const int high = std::max(lt.left, rt.left);
        const int low = std::min(lt.right, rt.right);
        lp = fixedAlign(flp, lp, lt, high, low);
        rp = fixedAlign(flp, rp, rt, high, low);
        AstNodeExpr* cmpp = nullptr;
        if (op == "=") {
            cmpp = new AstEq{flp, lp, rp};
        } else if (op == "/=") {
            cmpp = new AstNeq{flp, lp, rp};
        } else if (op == "<") {
            cmpp = isSigned ? static_cast<AstNodeExpr*>(new AstLtS{flp, lp, rp})
                            : static_cast<AstNodeExpr*>(new AstLt{flp, lp, rp});
        } else if (op == "<=") {
            cmpp = isSigned ? static_cast<AstNodeExpr*>(new AstLteS{flp, lp, rp})
                            : static_cast<AstNodeExpr*>(new AstLte{flp, lp, rp});
        } else if (op == ">") {
            cmpp = isSigned ? static_cast<AstNodeExpr*>(new AstGtS{flp, lp, rp})
                            : static_cast<AstNodeExpr*>(new AstGt{flp, lp, rp});
        } else {
            cmpp = isSigned ? static_cast<AstNodeExpr*>(new AstGteS{flp, lp, rp})
                            : static_cast<AstNodeExpr*>(new AstGte{flp, lp, rp});
        }
        cmpp->dtypep(bitDtp);
        return cmpp;
    }
    int left = 0;
    int right = 0;
    if (op == "+" || op == "-") {
        left = std::max(lt.left, rt.left) + 1;
        right = std::min(lt.right, rt.right);
        lp = fixedAlign(flp, lp, lt, left, right);
        rp = fixedAlign(flp, rp, rt, left, right);
    } else if (op == "*") {
        left = lt.left + rt.left + 1;
        right = lt.right + rt.right;
        // The operands are only extended: the product's weights are the sums of theirs
        const VType wide = vectorType(flp, VBasicDTypeKwd::LOGIC, lt.signing, left - right + 1);
        lp = resizeExpr(lp, wide, flp);
        rp = resizeExpr(rp, wide, flp);
    } else {
        unsupportedError(flp, "fixed-point operator '" + op + "'");
        return fail("");
    }
    const VType resultType = fixedType(flp, isSigned, left, right);
    AstNodeExpr* resultp = nullptr;
    if (op == "+") {
        resultp = new AstAdd{flp, lp, rp};
    } else if (op == "-") {
        resultp = new AstSub{flp, lp, rp};
    } else {
        resultp = isSigned ? static_cast<AstNodeExpr*>(new AstMulS{flp, lp, rp})
                           : static_cast<AstNodeExpr*>(new AstMul{flp, lp, rp});
    }
    resultp->dtypep(resultType.dtp);
    return noteFixed(expr, resultp, resultType);
}

AstNodeExpr* Lowering::lowerFixedUnary(FileLine* flp, const Expr& expr, AstNodeExpr* operandp,
                                       const VType& type) {
    if (expr.text != "-" && expr.text != "abs") {
        unsupportedError(flp, "fixed-point operator '" + expr.text + "'");
        operandp->deleteTree();
        return nullptr;
    }
    if (!type.isSigned()) {
        flp->v3error("VHDL: operator '" + expr.text + "' is not defined for ufixed.");
        operandp->deleteTree();
        return nullptr;
    }
    // The negation of the most negative value needs one more integer bit
    const VType resultType = fixedType(flp, true, type.left + 1, type.right);
    AstNodeExpr* const valuep = fixedAlign(flp, operandp, type, resultType.left, resultType.right);
    AstNodeExpr* resultp = new AstNegate{flp, valuep->cloneTree(false)};
    resultp->dtypep(resultType.dtp);
    if (expr.text == "abs") {
        AstCond* const condp
            = new AstCond{flp, fixedSel(flp, valuep->cloneTree(false), resultType.width - 1, 1),
                          resultp, valuep};
        condp->dtypep(resultType.dtp);
        resultp = condp;
    } else {
        valuep->deleteTree();
    }
    return noteFixed(expr, resultp, resultType);
}

//======================================================================
// Functions

namespace {
//! The overflow or rounding style a resize or to_[su]fixed argument names
bool fixedStyleName(const Expr& arg, bool& saturate, bool& round, bool& isOverflow) {
    if (arg.kind != Expr::NAME) return false;
    if (arg.text == "fixed_saturate" || arg.text == "fixed_wrap") {
        saturate = arg.text == "fixed_saturate";
        isOverflow = true;
        return true;
    }
    if (arg.text == "fixed_round" || arg.text == "fixed_truncate") {
        round = arg.text == "fixed_round";
        isOverflow = false;
        return true;
    }
    return false;
}
}  // namespace

bool Lowering::isFixedFunction(const std::string& name) const {
    return fixedPackageUsed()
           && (name == "resize" || name == "to_sfixed" || name == "to_ufixed" || name == "to_slv"
               || name == "to_sulv" || name == "to_signed" || name == "to_unsigned"
               || name == "to_integer");
}

//! A real value on the grid of a fixed-point type, rounded to nearest (or down) and saturated
AstNodeExpr* Lowering::fixedConstFromReal(FileLine* flp, double real, const VType& type,
                                          bool saturate, bool round) {
    const long double scaled = std::ldexp(static_cast<long double>(real), -type.right);
    long double rounded = round ? std::nearbyint(scaled) : std::floor(scaled);
    const bool isSigned = type.isSigned();
    const long double span = std::ldexp(1.0L, type.width - (isSigned ? 1 : 0));
    if (saturate) { rounded = std::min(std::max(rounded, isSigned ? -span : 0.0L), span - 1); }
    const int64_t value = static_cast<int64_t>(rounded);
    V3Number num{flp, type.width, 0u};
    for (int i = 0; i < type.width; ++i) {
        if ((value >> std::min(i, 63)) & 1) num.setBit(i, '1');
    }
    AstConst* const constp = new AstConst{flp, num};
    constp->dtypep(type.dtp);
    return constp;
}

//! `fixed op real` and `real op fixed`: the real takes the other operand's bounds
AstNodeExpr* Lowering::lowerFixedRealBinary(FileLine* flp, const Expr& expr, bool& handled) {
    handled = false;
    if (!fixedPackageUsed() || !expr.a || !expr.b) return nullptr;
    const bool realLeft = mentionsReal(*expr.a);
    if (realLeft == mentionsReal(*expr.b)) return nullptr;
    const Expr& realExpr = realLeft ? *expr.a : *expr.b;
    const Expr& fixedExpr = realLeft ? *expr.b : *expr.a;
    double real = 0;
    if (!evalReal(realExpr, real)) return nullptr;
    AstNodeExpr* const fixedp = lowerExpr(fixedExpr, nullptr);
    if (!fixedp) {
        handled = true;
        return nullptr;
    }
    const VType fixedT = typeOfLowered(fixedExpr, fixedp);
    if (!isFixedType(fixedT)) {
        fixedp->deleteTree();
        return nullptr;
    }
    handled = true;
    BinaryOperands ops;
    ops.lp = realLeft ? fixedConstFromReal(flp, real, fixedT, true, true) : fixedp;
    ops.rp = realLeft ? fixedp : fixedConstFromReal(flp, real, fixedT, true, true);
    ops.lt = fixedT;
    ops.rt = fixedT;
    return lowerFixedBinary(flp, expr, expr.text, ops);
}

AstNodeExpr* Lowering::lowerFixedCall(const Expr& expr, bool& handled) {
    handled = true;
    FileLine* const flp = expr.flp;
    const std::string& fn = expr.a ? expr.a->text : expr.text;
    const std::vector<ExprP>& argsp = expr.args;
    // The trailing style arguments, by position or by name
    bool saturate = true;  // fixed_pkg's defaults: fixed_saturate and fixed_round
    bool round = true;
    size_t positional = argsp.size();
    for (size_t i = 0; i < argsp.size(); ++i) {
        bool overflow = false;
        if (fixedStyleName(*argsp[i], saturate, round, overflow)) {
            positional = std::min(positional, i);
        }
    }
    // Static bounds or size: `(arg, left, right)`, `(arg, size_res)` or a lone `left`/`size`
    VType sizeRes;
    int64_t first = 0;
    int64_t second = 0;
    bool haveSize = false;  // A lone integer, or the length of a size_res vector
    bool haveBounds = false;
    if (positional >= 3 && staticInt(argsp[1], first) && staticInt(argsp[2], second)) {
        haveBounds = true;
    } else if (positional == 2 && fn != "resize" && staticInt(argsp[1], first)) {
        haveSize = true;
    } else if (positional == 2 && typeOfDesignator(*argsp[1], sizeRes)) {
        if (isFixedType(sizeRes)) {
            first = sizeRes.left;
            second = sizeRes.right;
            haveBounds = true;
        } else if (sizeRes.isVector()) {
            first = sizeRes.width;
            haveSize = true;
        }
    }
    // The argument first: a call that does not take a fixed-point value is another package's
    double real = 0;
    const bool realArg = (fn == "to_sfixed" || fn == "to_ufixed") && mentionsReal(*argsp[0]);
    AstNodeExpr* valuep = nullptr;
    VType type;
    if (realArg) {
        if (!evalReal(*argsp[0], real)) {
            unsupportedError(flp, "real argument of '" + fn + "' that is not static");
            return nullptr;
        }
    } else {
        valuep = lowerExpr(*argsp[0], nullptr);
        if (!valuep) return nullptr;
        type = typeOfLowered(*argsp[0], valuep);
        const bool toFixed = fn == "to_sfixed" || fn == "to_ufixed";
        if (!toFixed && !isFixedType(type)) {
            handled = false;  // numeric_std's resize, to_signed, ...
            valuep->deleteTree();
            return nullptr;
        }
    }
    const auto fail = [&](const std::string& message) -> AstNodeExpr* {
        flp->v3error("VHDL: " + message);
        if (valuep) valuep->deleteTree();
        return nullptr;
    };
    if (fn == "to_slv" || fn == "to_sulv") {
        VType slv = vectorType(flp, VBasicDTypeKwd::LOGIC, VSigning::NOSIGN, type.width);
        slv.typeName = "std_logic_vector";
        return noteFixed(expr, resizeExpr(valuep, slv, flp), slv);
    }
    if (fn == "resize") {
        if (!haveBounds) return fail("resize of a fixed-point value needs static bounds.");
        if (first < second) return fail("resize result has an ascending or null range.");
        const VType resultType
            = fixedType(flp, type.isSigned(), static_cast<int>(first), static_cast<int>(second));
        return noteFixed(
            expr,
            fixedResize(flp, valuep, type, resultType.left, resultType.right, saturate, round),
            resultType);
    }
    if (fn == "to_signed" || fn == "to_unsigned" || fn == "to_integer") {
        // The integer part, rounded and saturated like a resize onto bits `size-1 downto 0`
        const bool wantSigned = fn == "to_signed" || (fn == "to_integer" && type.isSigned());
        int size = 0;
        if (fn == "to_integer") {
            size = type.isSigned() ? 32 : 31;
        } else if (haveSize && first > 0) {
            size = static_cast<int>(first);
        } else {
            return fail(fn + " needs a static result size.");
        }
        if ((fn == "to_signed" && !type.isSigned()) || (fn == "to_unsigned" && type.isSigned())) {
            return fail(fn + " does not take this fixed-point type.");
        }
        AstNodeExpr* const resizedp = fixedResize(flp, valuep, type, size - 1, 0, saturate, round);
        const VType numeric = vectorType(flp, VBasicDTypeKwd::LOGIC,
                                         wantSigned ? VSigning::SIGNED : VSigning::UNSIGNED, size);
        AstNodeExpr* const castp = resizeExpr(resizedp, numeric, flp);
        if (fn == "to_integer") { return resizeExpr(castp, intType(flp), flp); }
        VType named = numeric;
        named.typeName = wantSigned ? "signed" : "unsigned";
        return noteFixed(expr, castp, named);
    }
    // to_sfixed / to_ufixed
    const bool toSigned = fn == "to_sfixed";
    if (!haveBounds && !haveSize && argsp.size() != 1) {
        return fail(fn + " needs static bounds.");
    }
    int left = 0;
    int right = 0;
    if (haveBounds) {
        left = static_cast<int>(first);
        right = static_cast<int>(second);
    } else if (haveSize) {
        left = static_cast<int>(first);  // A lone left index; the right index is 0
    } else if (!realArg && isFixedType(type)) {
        // ufixed to sfixed gains a sign bit
        left = type.left + (toSigned && !type.isSigned() ? 1 : 0);
        right = type.right;
    } else if (!realArg && type.isVector()) {
        left = type.width - 1;  // A signed or unsigned argument: bits `width-1 downto 0`
        right = 0;
    } else {
        return fail(fn + " needs static bounds.");
    }
    if (left < right) return fail(fn + " result has an ascending or null range.");
    const VType resultType = fixedType(flp, toSigned, left, right);
    if (realArg) {
        return noteFixed(expr, fixedConstFromReal(flp, real, resultType, saturate, round),
                         resultType);
    }
    if (type.isInt()) {
        return noteFixed(expr,
                         fixedResize(flp, valuep, fixedType(flp, toSigned, 31, 0), resultType.left,
                                     resultType.right, saturate, round),
                         resultType);
    }
    if (isFixedType(type)) {
        // ufixed to sfixed, or the same signedness: the same value on the new grid
        AstNodeExpr* const gridp
            = fixedAlign(flp, valuep, type, resultType.left, resultType.right);
        return noteFixed(expr, resizeExpr(gridp, resultType, flp), resultType);
    }
    const bool numeric = type.typeName == "signed" || type.typeName == "unsigned"
                         || type.typeName == "u_signed" || type.typeName == "u_unsigned";
    if (numeric && haveBounds) {
        // A signed or unsigned value converts as an integer: bits `width-1 downto 0`
        const VType asFixed = fixedType(flp, type.isSigned(), type.width - 1, 0);
        return noteFixed(
            expr,
            fixedResize(flp, valuep, asFixed, resultType.left, resultType.right, saturate, round),
            resultType);
    }
    if (type.isVector()) {
        // Any other vector takes the bounds bit for bit
        if (type.width != resultType.width) {
            return fail(fn + " argument has " + std::to_string(type.width)
                        + " bits, but the result bounds name " + std::to_string(resultType.width)
                        + ".");
        }
        return noteFixed(expr, resizeExpr(valuep, resultType, flp), resultType);
    }
    return fail(fn + " of an unsupported argument type.");
}

}  // namespace vhdlnative
