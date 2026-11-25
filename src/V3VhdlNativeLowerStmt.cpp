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
// Lowers VHDL sequential and concurrent statements into the V3 AST
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

bool Lowering::concHasEdge(const Concurrent& conc) const {
    for (const auto& alt : conc.alternatives) {
        if (alt.second && containsEdgeCall(*alt.second)) return true;
    }
    return false;
}

//! A conditional assignment gated by a clock edge is a register: if/elsif with
//! non-blocking assignments under the edge sensitivity
AstNodeStmt* Lowering::lowerClockedAlternatives(const Concurrent& conc, const VType& type,
                                                AstNodeExpr* lhsp) {
    FileLine* const flp = conc.flp;
    // Alternatives before the clocked one are level branches, e.g. an asynchronous
    // reset; they assign immediately, the clocked branches wait for the edge
    std::size_t firstEdged = conc.alternatives.size();
    for (std::size_t i = 0; i < conc.alternatives.size(); ++i) {
        const auto& alt = conc.alternatives[i];
        if (alt.second && containsEdgeCall(*alt.second)) {
            firstEdged = i;
            break;
        }
    }
    AstNodeStmt* restp = nullptr;
    for (std::size_t i = conc.alternatives.size(); i-- > 0;) {
        const auto& alt = conc.alternatives[i];
        AstNodeExpr* valuep = lowerExpr(*alt.first, &type);
        if (!valuep) {
            if (restp) restp->deleteTree();
            return nullptr;
        }
        if (alt.first->kind != Expr::UNAFFECTED
            && !valueConforms(type, *alt.first, "assignment value")) {
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, type, flp);
        const bool level = i < firstEdged;
        AstNodeStmt* const assignp
            = level ? static_cast<AstNodeStmt*>(new AstAssign{flp, lhsp->cloneTree(false), valuep})
                    : static_cast<AstNodeStmt*>(
                          new AstAssignDly{flp, lhsp->cloneTree(false), valuep});
        if (!alt.second) {  // The final else
            if (restp) restp->deleteTree();
            restp = assignp;
            continue;
        }
        AstNodeExpr* const condp = lowerCond(*alt.second);
        if (!condp) {
            assignp->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        restp = new AstIf{flp, condp, assignp, restp};
    }
    return restp;
}

//! A conditional signal assignment with no final `else` is a process that keeps the
//! target value when no condition holds. Emit it as a latch, so the hold is explicit
//! rather than feeding the target back into a continuous assignment.
AstNodeStmt* Lowering::lowerLatchedAlternatives(const Concurrent& conc, const VType& type,
                                                AstNodeExpr* lhsp) {
    FileLine* const flp = conc.flp;
    AstNodeStmt* restp = nullptr;
    for (std::size_t i = conc.alternatives.size(); i-- > 0;) {
        const auto& alt = conc.alternatives[i];
        if (alt.first->kind == Expr::UNAFFECTED) {
            // The branch holds the target: an empty statement under its condition
            AstNodeStmt* const holdp = new AstComment{flp, "unaffected"};
            if (!alt.second) {
                if (restp) restp->deleteTree();
                restp = holdp;
                continue;
            }
            AstNodeExpr* const condp = lowerCond(*alt.second);
            if (!condp) {
                holdp->deleteTree();
                if (restp) restp->deleteTree();
                return nullptr;
            }
            restp = new AstIf{flp, condp, holdp, restp};
            continue;
        }
        AstNodeExpr* valuep = lowerExpr(*alt.first, &type);
        if (!valuep) {
            if (restp) restp->deleteTree();
            return nullptr;
        }
        if (!valueConforms(type, *alt.first, "assignment value")) {
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, type, flp);
        AstNodeStmt* const assignp = new AstAssign{flp, lhsp->cloneTree(false), valuep};
        if (!alt.second) {
            if (restp) restp->deleteTree();
            restp = assignp;
            continue;
        }
        AstNodeExpr* const condp = lowerCond(*alt.second);
        if (!condp) {
            assignp->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        restp = new AstIf{flp, condp, assignp, restp};
    }
    return restp;
}

AstNodeExpr* Lowering::lowerAlternatives(const Concurrent& conc, const VType& type) {
    FileLine* const flp = conc.flp;
    if (conc.alternatives.empty()) return nullptr;
    if (containsEdgeCall(*conc.alternatives.front().first)) {
        unsupportedError(flp, "conditional signal assignment gated by a clock edge");
        return nullptr;
    }
    AstNodeExpr* restp = nullptr;  // Value when no alternative is taken
    for (std::size_t i = conc.alternatives.size(); i-- > 0;) {
        const auto& alt = conc.alternatives[i];
        if (alt.second && containsEdgeCall(*alt.second)) {
            unsupportedError(flp, "conditional signal assignment gated by a clock edge");
            if (restp) restp->deleteTree();
            return nullptr;
        }
        AstNodeExpr* valuep = lowerExpr(*alt.first, &type);
        if (!valuep) {
            if (restp) restp->deleteTree();
            return nullptr;
        }
        if (alt.first->kind != Expr::UNAFFECTED
            && !valueConforms(type, *alt.first, "assignment value")) {
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, type, flp);
        if (!alt.second) {
            // The final else: it decides the value when no condition holds
            if (restp) restp->deleteTree();
            restp = valuep;
            continue;
        }
        AstNodeExpr* condp = lowerExpr(*alt.second, nullptr);
        if (!condp) {
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        if (condp->width() != 1) {
            condp->v3error("VHDL: condition is not a 1-bit value.");
            condp->deleteTree();
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        AstCond* const condexprp = new AstCond{flp, condp, valuep, restp};
        condexprp->dtypep(type.dtp);
        restp = condexprp;
    }
    return restp;
}

//! Index range an aggregate covers, for a vector mark with no constraint: the
//! lowest index becomes the left bound and the range is ascending
bool Lowering::aggregateBounds(const Expr& agg, int64_t& low, int64_t& high) const {
    bool any = false;
    const auto include = [&](int64_t first, int64_t last) {
        const int64_t lo = std::min(first, last);
        const int64_t hi = std::max(first, last);
        low = any ? std::min(low, lo) : lo;
        high = any ? std::max(high, hi) : hi;
        any = true;
    };
    if (agg.kind == Expr::AGG_POSITIONAL) {
        // Positional associations take consecutive indices from zero
        for (size_t i = 0; i < agg.args.size(); ++i) {
            if (i < agg.choices.size() && !agg.choices[i].empty()) return false;
            include(0, static_cast<int64_t>(i));
        }
        return any;
    }
    for (size_t i = 0; i < agg.args.size() && i < agg.choices.size(); ++i) {
        for (const ExprP& choicep : agg.choices[i]) {
            if (!choicep) continue;
            if (choicep->kind == Expr::NAME && choicep->text == "others") return false;
            if (choicep->kind == Expr::SLICE) {
                int64_t left = 0;
                int64_t right = 0;
                if (!staticInt(choicep->b, left) || !staticInt(choicep->c, right)) return false;
                include(left, right);
                continue;
            }
            int64_t index = 0;
            if (!evalInt(*choicep, index)) return false;
            include(index, index);
        }
    }
    return any;
}

void Lowering::constrainAggregateType(const Expr& agg, VType& type) const {
    if (agg.kind != Expr::AGG_POSITIONAL && agg.kind != Expr::AGG_NAMED) return;
    if (type.kind != VType::ARRAY || type.width > 0 || !type.elemp) return;
    int64_t low = 0;
    int64_t high = 0;
    // An `others` association or a non-static choice leaves the length to the context
    if (!aggregateBounds(agg, low, high)) return;
    if (agg.kind == Expr::AGG_POSITIONAL) {
        // Positional associations start at the index subtype's low bound
        low += type.intLow;
        high += type.intLow;
    }
    type.left = static_cast<int>(low);
    type.right = static_cast<int>(high);
    // An unconstrained index subtype is ascending, so the choices give ascending bounds
    // (IEEE 1076-2008 9.3.3.3)
    type.ascending = true;
    type.width = type.elemWidth * (std::abs(type.left - type.right) + 1);
    type.dtp = findVectorWidth(agg.flp, type.kwd, type.signing, type.width);
}

bool Lowering::lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                                 std::vector<AstConst*>& cops) {
    VType choiceType;
    const bool choiceEnum = enumTypeOfDType(dtp, choiceType);
    if (!choiceEnum) choiceType = typeOfDType(dtp);
    // A source spelling for the choices of one item, so a value shared by two different
    // literals can be told apart from a choice written twice
    const auto choiceText = [](const Expr& expr) -> std::string {
        if (expr.kind == Expr::CHAR_LIT) return std::string{"'"} + expr.charValue + "'";
        if (expr.kind == Expr::NAME) return expr.text;
        if (expr.kind == Expr::INT_LIT) return std::to_string(expr.intValue);
        return std::string{};
    };
    std::map<int64_t, std::string> seenValues;  // Value -> the text that named it
    for (const ExprP& choicep : choices) {
        // A string literal whose characters are all logic characters is a bit vector in a
        // logic context and one character per element of a string selector, as `"ZU"` is
        // for a `string(3 to 4)` selector (IEEE 1076-2008 9.3.2, 15.5)
        if (choicep->kind == Expr::BITS_LIT && !choicep->spelling.empty() && dtp
            && dtp->width() == 8 * static_cast<int>(choicep->spelling.size())
            && dtp->width() != static_cast<int>(choicep->bits.size())) {
            AstNodeExpr* const stringp = makeStringConst(choicep->flp, choicep->spelling);
            AstConst* const constp = VN_CAST(stringp, Const);
            constp->dtypep(dtp);
            cops.push_back(constp);
            continue;
        }
        if (choicep->kind == Expr::SLICE) {
            // A choice range expands to one case item per value
            int64_t left = 0;
            int64_t right = 0;
            if (!staticInt(choicep->b, left) || !staticInt(choicep->c, right)) {
                unsupportedError(choicep->flp, "non-static choice range");
                return false;
            }
            const int64_t step = choicep->text == "to" ? 1 : -1;
            if ((right - left) * step < 0) {
                choicep->flp->v3error("VHDL: empty choice range.");
                return false;
            }
            for (int64_t value = left;; value += step) {
                AstNodeExpr* const exprp = makeVectorConst(choicep->flp, choiceType, value);
                AstConst* const constp = VN_CAST(exprp, Const);
                cops.push_back(constp);
                if (value == right) break;
            }
            continue;
        }
        AstNodeExpr* const exprp = lowerExpr(*choicep, &choiceType);
        if (!exprp) return false;
        AstConst* constp = VN_CAST(exprp, Const);
        if (!constp) {
            // A choice may be an expression of constants: `f(6 downto 4) & f(2 downto 0)`
            AstConst* const foldedp = foldStatic(exprp);
            exprp->deleteTree();
            if (!foldedp) {
                unsupportedError(choicep->flp, "non-static selected-assignment choice");
                return false;
            }
            constp = foldedp;
        }
        if (constp->dtypep()->width() != dtp->width()) constp->dtypep(dtp);
        // A metavalue choice has no two-state ordinal and cannot be matched by a signal
        // in the cycle model; report it instead of asserting in toUInt()
        if (constp->num().isFourState()) {
            unsupportedError(choicep->flp, "metavalue in a case or selected choice");
            constp->deleteTree();
            return false;
        }
        // The nine-valued std_logic is modelled with two states, so two different literals
        // can name one value ('0' and 'L', '1' and 'H'): the pair is a single choice then,
        // not the duplicate the standard rejects (IEEE 1076-2008 10.9)
        if (constp->width() <= 32) {
            const int64_t value = constp->num().isSigned()
                                      ? static_cast<int64_t>(constp->num().toSInt())
                                      : static_cast<int64_t>(constp->num().toUInt());
            const std::string text = choiceText(*choicep);
            const auto seen = seenValues.find(value);
            if (seen != seenValues.end() && !seen->second.empty() && !text.empty()
                && seen->second != text) {
                constp->deleteTree();
                continue;
            }
            seenValues[value] = text;
        }
        cops.push_back(constp);
    }
    return true;
}

//! Whether a lowered value contains a high-impedance constant.
bool astHasZ(AstNode* nodep) {
    bool found = false;
    nodep->foreach([&](AstConst* constp) {
        if (constp->num().hasZ()) found = true;
    });
    return found;
}

bool Lowering::addChoiceValues(const std::vector<AstConst*>& cops, std::set<int64_t>& covered,
                               const char* what) {
    bool ok = true;
    for (AstConst* const cop : cops) {
        // A value too wide to enumerate is not tracked; such a selector needs `others`
        if (cop->width() > 32) continue;
        const int64_t value = cop->num().isSigned() ? static_cast<int64_t>(cop->num().toSInt())
                                                    : static_cast<int64_t>(cop->num().toUInt());
        if (covered.insert(value).second) continue;
        cop->v3error(std::string{"VHDL: duplicate choice in a "} + what
                     + " (IEEE 1076-2008 10.9).");
        ok = false;
    }
    return ok;
}

bool Lowering::choicesComplete(AstNodeDType* selDtp, const VType& selType,
                               const std::set<int64_t>& covered) const {
    VType enumType;
    if (enumTypeOfDType(selDtp, enumType)) {
        for (size_t i = 0; i < enumType.literals.size(); ++i) {
            if (!covered.count(static_cast<int64_t>(i))) return false;
        }
        return true;
    }
    VType type = typeOfDType(selDtp);
    if (selType.kind == VType::INT) {
        // An integer selector's choices must cover its subtype, not the base 32-bit
        // type (IEEE 1076-2008 10.9); a range too wide to enumerate needs `others`
        const int64_t span = static_cast<int64_t>(selType.intHigh) - selType.intLow + 1;
        if (span <= 0 || span > static_cast<int64_t>(covered.size())) return false;
        for (int64_t value = selType.intLow; value <= selType.intHigh; ++value) {
            if (!covered.count(value)) return false;
        }
        return true;
    }
    if (selType.isVector() && selType.width > 0) type = selType;
    if (type.kind == VType::LOGIC || type.kind == VType::BIT) {
        return covered.count(0) && covered.count(1);
    }
    if (type.isVector() && type.width > 0 && type.width <= 16) {
        return covered.size() == (size_t{1} << type.width);
    }
    return false;
}
AstNodeStmt* Lowering::makeRuntimeErrorStmt(FileLine* flp, AstNodeExpr* condp,
                                            const std::string& message) {
    AstNodeStmt* bodyp
        = new AstDisplay{flp, VDisplayType::DT_ERROR, message, nullptr, nullptr, 'd'};
    bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, new AstFinish{flp});
    return new AstIf{flp, condp, bodyp, nullptr};
}

void Lowering::addRuntimeError(FileLine* flp, AstNodeExpr* condp, const std::string& message) {
    m_modp->addStmtsp(new AstAlways{flp, VAlwaysKwd::ALWAYS_COMB, nullptr,
                                    makeRuntimeErrorStmt(flp, condp, message)});
}

void checkDrivers(AstNetlist* rootp, const Design& design) {
    // A composite signal's scalar subelements are separate signals (IEEE 1076-2008
    // 6.4.2.3), so drivers are compared per bit position. Each emitted always/initial
    // statement and each bare continuous assignment is one driver; a write whose index
    // is not static is taken to cover the whole signal. A declared initial value is not
    // a driver. Every unit is counted, because a package signal is driven from several.
    struct Range final {
        int lsb = 0;  // Least significant bit of the driven range
        int width = 0;  // Bit width of the driven range
        int driver = 0;  // Driver that writes the range
    };
    std::map<AstVar*, std::vector<Range>> ranges;
    int driver = 0;
    for (AstNodeModule* const modp : rootp->modulespList()) {
        for (AstNode* stmtp = modp->stmtsp(); stmtp; stmtp = stmtp->nextp()) {
            if (VN_IS(stmtp, InitialStatic) || VN_IS(stmtp, Var) || VN_IS(stmtp, Cell)) continue;
            if (!VN_IS(stmtp, Always) && !VN_IS(stmtp, Initial) && !VN_IS(stmtp, AssignW)
                && !VN_IS(stmtp, AssignDly) && !VN_IS(stmtp, Assign)) {
                continue;
            }
            ++driver;
            stmtp->foreach([&](AstVarRef* refp) {
                if (refp->access() != VAccess::WRITE) return;
                AstVar* const varp = refp->varp();
                if (!design.unresolvedSignals.count(varp)) return;
                Range range;
                range.driver = driver;
                range.width = varp->width();
                // Compose the selects around the write, so a record element or array element
                // covers only its own bits; the indices are folded here rather than later
                AstNode* nodep = refp;
                while (AstSel* const selp = VN_CAST(nodep->backp(), Sel)) {
                    if (selp->fromp() != nodep) break;
                    AstConst* const lsbp = Lowering::foldStatic(selp->lsbp());
                    if (!lsbp) break;
                    range.lsb += static_cast<int>(lsbp->toUInt());
                    lsbp->deleteTree();
                    range.width = selp->widthConst();
                    nodep = selp;
                }
                ranges[varp].push_back(range);
            });
        }
    }
    for (const auto& entry : ranges) {
        const std::vector<Range>& vec = entry.second;
        bool reported = false;
        for (size_t i = 0; i < vec.size() && !reported; ++i) {
            for (size_t j = i + 1; j < vec.size(); ++j) {
                if (vec[i].driver == vec[j].driver) continue;
                if (vec[i].lsb >= vec[j].lsb + vec[j].width) continue;
                if (vec[j].lsb >= vec[i].lsb + vec[i].width) continue;
                entry.first->v3error("VHDL: unresolved signal " + entry.first->prettyNameQ()
                                     + " has more than one driver (IEEE 1076-2008 6.4.2.3).");
                reported = true;
                break;
            }
        }
    }
}

AstNodeStmt* Lowering::lowerSelected(const Concurrent& conc) {
    FileLine* const flp = conc.flp;
    SelectedLowering ctx;
    ctx.flp = flp;
    ctx.concp = &conc;
    ctx.selectorp = lowerExpr(*conc.selector, nullptr);
    if (!ctx.selectorp) return nullptr;
    ctx.lhsp = lowerDesignator(*conc.target, true);
    if (!ctx.lhsp) {
        ctx.selectorp->deleteTree();
        return nullptr;
    }
    ctx.targetType = typeOfTarget(*conc.target, ctx.lhsp);
    ctx.selDtp = ctx.selectorp->dtypep();
    if (!actualType(*conc.selector, ctx.selectorType)) {
        ctx.selectorType = typeOfDType(ctx.selDtp);
    }
    if (!discreteSelectorType(ctx.selectorType)) {
        flp->v3error("VHDL: selected assignment selector is not discrete "
                     "(IEEE 1076-2008 10.9).");
        ctx.lhsp->deleteTree();
        ctx.selectorp->deleteTree();
        return nullptr;
    }
    if (!lowerSelectedValues(ctx)) return nullptr;
    return ctx.tristate ? lowerSelectedTristate(ctx) : lowerSelectedCase(ctx);
}

//! Lower each alternative's value once, checking it against the target's type
bool Lowering::lowerSelectedValues(SelectedLowering& ctx) {
    const Concurrent& conc = *ctx.concp;
    // Lower every value once. Checking the lowered value also catches a high-impedance
    // constant produced by a folded call, not only a literal in the source.
    ctx.values.reserve(conc.choices.size());
    for (const auto& alternative : conc.choices) {
        AstNodeExpr* valuep = lowerExpr(*alternative.second, &ctx.targetType);
        if (!valuep) {
            ctx.deleteValues(0);
            ctx.lhsp->deleteTree();
            ctx.selectorp->deleteTree();
            return false;
        }
        if (!valueConforms(ctx.targetType, *alternative.second, "selected assignment value")) {
            valuep->deleteTree();
            ctx.deleteValues(0);
            ctx.lhsp->deleteTree();
            ctx.selectorp->deleteTree();
            return false;
        }
        valuep = resizeExpr(valuep, ctx.targetType, ctx.flp);
        if (astHasZ(valuep)) ctx.tristate = true;
        ctx.values.push_back(valuep);
    }
    return true;
}

//! Lower a selected assignment with a high-impedance value as a conditional expression
AstNodeStmt* Lowering::lowerSelectedTristate(SelectedLowering& ctx) {
    FileLine* const flp = ctx.flp;
    const Concurrent& conc = *ctx.concp;
    std::vector<AstNodeExpr*>& values = ctx.values;
    // V3Tristate derives the output enable from a conditional expression, not from a
    // case item, so a selected assignment that releases the driver is lowered to the
    // equivalent if/elsif chain.
    AstNodeExpr* restp = nullptr;
    std::set<int64_t> covered;
    bool bad = false;
    for (std::size_t i = conc.choices.size(); i-- > 0;) {
        AstNodeExpr* const valuep = values[i];
        values[i] = nullptr;
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(conc.choices[i].first, ctx.selDtp, cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            valuep->deleteTree();
            ctx.deleteValues(0);
            if (restp) restp->deleteTree();
            ctx.lhsp->deleteTree();
            ctx.selectorp->deleteTree();
            return nullptr;
        }
        if (cops.empty()) {  // The `others` alternative is the fall-through
            if (restp) {
                // A second `others` can never be selected (IEEE 1076-2008 11.6)
                flp->v3error("VHDL: more than one 'others' choice in a selected "
                             "assignment.");
                bad = true;
                valuep->deleteTree();
            } else {
                restp = valuep;
            }
            continue;
        }
        if (!addChoiceValues(cops, covered, "selected assignment")) bad = true;
        AstNodeExpr* condp = nullptr;
        for (AstConst* const cop : cops) {
            AstEq* const eqp = new AstEq{flp, ctx.selectorp->cloneTree(false), cop};
            eqp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            if (!condp) {
                condp = eqp;
            } else {
                AstOr* const orp = new AstOr{flp, condp, eqp};
                orp->dtypep(eqp->dtypep());
                condp = orp;
            }
        }
        AstCond* const condexprp = new AstCond{flp, condp, valuep, restp};
        condexprp->dtypep(ctx.targetType.dtp);
        restp = condexprp;
    }
    ctx.selectorp->deleteTree();
    if (!restp) {
        // A selected assignment without `others` must be exhaustive; the
        // fall-through is then unreachable, but a conditional expression needs one.
        if (bad || !choicesComplete(ctx.selDtp, ctx.selectorType, covered)) {
            flp->v3error("VHDL: selected assignment does not cover every value of "
                         "its selector (IEEE 1076-2008 11.6).");
            ctx.lhsp->deleteTree();
            return nullptr;
        }
        restp = makeRepeatedConst(flp, ctx.targetType, '0');
    } else if (bad) {
        restp->deleteTree();
        ctx.lhsp->deleteTree();
        return nullptr;
    }
    // The caller wraps this in the conditional-assignment always
    return new AstAssignW{flp, ctx.lhsp, restp};
}

//! Lower a selected assignment without high-impedance values as a case statement
AstNodeStmt* Lowering::lowerSelectedCase(SelectedLowering& ctx) {
    FileLine* const flp = ctx.flp;
    const Concurrent& conc = *ctx.concp;
    std::vector<AstNodeExpr*>& values = ctx.values;
    AstCaseItem* itemsp = nullptr;
    AstCaseItem* currentp = nullptr;
    std::set<int64_t> covered;
    bool bad = false;
    for (std::size_t i = 0; i < conc.choices.size(); ++i) {
        AstNodeExpr* const valuep = values[i];
        values[i] = nullptr;
        AstNodeStmt* const assignp = new AstAssignW{flp, ctx.lhsp->cloneTree(false), valuep};
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(conc.choices[i].first, ctx.selDtp, cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            assignp->deleteTree();
            ctx.deleteValues(i + 1);
            if (itemsp) itemsp->deleteTree();
            ctx.lhsp->deleteTree();
            ctx.selectorp->deleteTree();
            return nullptr;
        }
        if (cops.empty() && currentp) {
            // A second "others" alternative can never be selected (IEEE 1076-2008 11.6)
            flp->v3error("VHDL: more than one 'others' choice in a selected assignment.");
            bad = true;
            assignp->deleteTree();
            continue;
        }
        if (!addChoiceValues(cops, covered, "selected assignment")) bad = true;
        AstCaseItem* const itemp = cops.empty() ? new AstCaseItem{flp, nullptr, assignp}
                                                : new AstCaseItem{flp, cops.front(), assignp};
        for (size_t j = 1; j < cops.size(); ++j) itemp->addCondsp(cops[j]);
        if (cops.empty()) currentp = itemp;
        itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, itemp) : itemp;
    }
    ctx.lhsp->deleteTree();
    if (!itemsp) {
        ctx.selectorp->deleteTree();
        return nullptr;
    }
    if (!bad && !currentp && !choicesComplete(ctx.selDtp, ctx.selectorType, covered)) {
        flp->v3error("VHDL: selected assignment does not cover every value of its selector "
                     "(IEEE 1076-2008 11.6).");
        bad = true;
    }
    if (bad) {
        itemsp->deleteTree();
        ctx.selectorp->deleteTree();
        return nullptr;
    }
    return makeCase(flp, ctx.selectorp, itemsp);
}

AstNodeStmt* Lowering::lowerAggregateAssign(FileLine* flp, const Expr& target, const Expr& value,
                                            bool variable, bool concurrentProc) {
    if (target.kind != Expr::AGG_POSITIONAL) {
        unsupportedError(flp, "aggregate assignment target with choices");
        return nullptr;
    }
    std::vector<AstNodeExpr*> lhsv;
    int totalWidth = 0;
    bool failed = false;
    for (const ExprP& elementp : target.args) {
        if (!elementp) continue;
        if (elementp->kind == Expr::AGG_POSITIONAL || elementp->kind == Expr::AGG_NAMED) {
            unsupportedError(flp, "nested aggregate assignment target");
            failed = true;
            break;
        }
        AstNodeExpr* const lhsp = lowerDesignator(*elementp, true);
        if (!lhsp) {
            failed = true;
            break;
        }
        lhsv.push_back(lhsp);
        totalWidth += lhsp->width();
    }
    if (failed || lhsv.empty() || totalWidth == 0) {
        for (AstNodeExpr* const lhsp : lhsv) lhsp->deleteTree();
        return nullptr;
    }
    const VType totalType = vectorType(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, totalWidth);
    AstNodeExpr* valuep = lowerExpr(value, &totalType);
    if (!valuep) {
        for (AstNodeExpr* const lhsp : lhsv) lhsp->deleteTree();
        return nullptr;
    }
    valuep = resizeExpr(valuep, totalType, flp);
    AstNodeStmt* headp = nullptr;
    int offset = totalWidth;
    for (AstNodeExpr* const lhsp : lhsv) {
        const int width = lhsp->width();
        offset -= width;
        AstNodeExpr* partp = nullptr;
        if (width == totalWidth) {
            partp = valuep->cloneTree(false);
        } else {
            AstSel* const selp = new AstSel{flp, valuep->cloneTree(false), offset, width};
            selp->dtypep(lhsp->dtypep());
            partp = selp;
        }
        AstNodeStmt* assignp = nullptr;
        if (variable) {
            assignp = new AstAssign{flp, lhsp, partp};
        } else if (concurrentProc) {
            assignp = new AstAssignW{flp, lhsp, partp};
        } else {
            assignp = new AstAssignDly{flp, lhsp, partp};
        }
        headp = headp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, assignp) : assignp;
    }
    valuep->deleteTree();
    return headp;
}

AstNodeStmt* Lowering::lowerAssign(const Stmt& stmt, bool concurrent, bool variable) {
    if (m_passive) {
        stmt.flp->v3error("VHDL: assignment in the entity statement part.");
        return nullptr;
    }
    // A delay of zero is a delta, which a cycle model already has; a longer one needs a
    // scheduled update, which a process body has no notion of
    if (stmt.delay && !staticZeroDelay(stmt.delay)) {
        unsupportedError(stmt.flp, "signal assignment with a delay");
        return nullptr;
    }
    if (stmt.target->kind == Expr::AGG_POSITIONAL || stmt.target->kind == Expr::AGG_NAMED) {
        return lowerAggregateAssign(stmt.flp, *stmt.target, *stmt.value, variable,
                                    m_concurrentProc);
    }
    // A slice whose bounds cross is a null array (IEEE 1076-2008 5.3.2): a loop that
    // shifts a pipeline writes `p(2 to N)` and, with N=1, `p(2 to 1)`, which assigns
    // nothing
    if (stmt.target->kind == Expr::SLICE && stmt.target->b && stmt.target->c) {
        int64_t left = 0;
        int64_t right = 0;
        if (staticIntFold(stmt.target->b, left) && staticIntFold(stmt.target->c, right)) {
            const bool empty = stmt.target->text == "downto" ? left < right : left > right;
            if (empty) return nullptr;
        }
    }
    // A whole null array is likewise a no-op; skip it before its aggregate value is
    // lowered into a zero-width expression
    {
        VType targetType;
        if (actualType(*stmt.target, targetType) && targetType.isVector()
            && targetType.width == 0) {
            return nullptr;
        }
    }
    // A bare `x <= unaffected;` leaves the target unchanged, so emit nothing
    if (stmt.alternatives.empty() && stmt.value && stmt.value->kind == Expr::UNAFFECTED) {
        return nullptr;
    }
    // A slice whose width is only known at run time cannot be one selection node
    if (stmt.target->kind == Expr::SLICE && stmt.target->a && stmt.target->b && stmt.target->c
        && stmt.alternatives.empty()) {
        int64_t count = 0;
        if (!sliceWidth(*stmt.target->b, *stmt.target->c, stmt.target->text != "downto", count)) {
            return lowerDynamicSliceAssign(stmt, variable);
        }
    }
    AstNodeExpr* lhsp = lowerDesignator(*stmt.target, true);
    if (!lhsp) return nullptr;
    const VType ltype = typeOfTarget(*stmt.target, lhsp);
    AstNodeExpr* rhsp = nullptr;
    if (!stmt.alternatives.empty()) {
        // `unaffected` means the target's current value, so offer a read of the target
        AstNodeExpr* const targetReadp = lowerDesignator(*stmt.target, false);
        rhsp = lowerConditionalChain(stmt.flp, stmt.alternatives, ltype, targetReadp);
        if (targetReadp) targetReadp->deleteTree();
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
        return nullptr;
    }
    // A null array has no elements, so assigning to it is a no-op (IEEE 1076-2008
    // 5.3.2); emitting a zero-width assignment would fail in V3Width
    if (ltype.isVector() && ltype.width == 0) {
        lhsp->deleteTree();
        rhsp->deleteTree();
        return nullptr;
    }
    // Only variable assignments update immediately; a signal write waits for the
    // delta even in the time-zero activation (IEEE 1076-2008 10.5)
    if (variable) return new AstAssign{stmt.flp, lhsp, rhsp};
    // A concurrent procedure call drives its signal actuals as continuous assignments
    if (m_concurrentProc) return new AstAssignW{stmt.flp, lhsp, rhsp};
    return new AstAssignDly{stmt.flp, lhsp, rhsp};
}

AstNodeExpr*
Lowering::lowerConditionalChain(FileLine* flp,
                                const std::vector<std::pair<ExprP, ExprP>>& alternatives,
                                const VType& type, AstNodeExpr* targetReadp) {
    AstNodeExpr* restp = nullptr;  // Value when no condition holds
    for (std::size_t i = alternatives.size(); i-- > 0;) {
        const auto& alt = alternatives[i];
        AstNodeExpr* valuep = nullptr;
        if (alt.first->kind == Expr::UNAFFECTED) {
            if (!targetReadp) {
                unsupportedError(flp, "'unaffected' assignment to a variable");
                if (restp) restp->deleteTree();
                return nullptr;
            }
            valuep = static_cast<AstNodeExpr*>(targetReadp->cloneTree(false));
        } else {
            valuep = lowerExpr(*alt.first, &type);
            if (!valuep) {
                if (restp) restp->deleteTree();
                return nullptr;
            }
            if (!valueConforms(type, *alt.first, "assignment value")) {
                valuep->deleteTree();
                if (restp) restp->deleteTree();
                return nullptr;
            }
            valuep = resizeExpr(valuep, type, flp);
        }
        if (!alt.second) {
            if (restp) restp->deleteTree();
            restp = valuep;
            continue;
        }
        AstNodeExpr* const condp = lowerExpr(*alt.second, nullptr);
        if (!condp) {
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        if (condp->width() != 1) {
            condp->v3error("VHDL: condition is not a 1-bit value.");
            condp->deleteTree();
            valuep->deleteTree();
            if (restp) restp->deleteTree();
            return nullptr;
        }
        AstCond* const condexprp = new AstCond{flp, condp, valuep, restp};
        condexprp->dtypep(type.dtp);
        restp = condexprp;
    }
    return restp;
}

AstNodeStmt* Lowering::lowerIf(const Stmt& stmt) {
    AstNodeStmt* elsesp = nullptr;
    // A value tracked before the statement still holds inside each branch, which are
    // lowered in reverse, so each one starts from the same values
    const auto entryVals = saveVarVals();
    // A branch runs only if no earlier condition held, so an earlier condition over
    // constants that did not fold leaves it undecided as well
    std::vector<bool> unfolded(stmt.branches.size(), false);
    bool undecided = false;
    for (std::size_t i = 0; i < stmt.branches.size(); ++i) {
        const auto& cond = stmt.branches[i].first;
        bool staticValue = false;
        if (cond && !evalCond(*cond, staticValue)) {
            std::set<std::string> names;
            collectExprNames(*cond, names);
            undecided = undecided
                        || std::none_of(names.begin(), names.end(), [&](const std::string& name) {
                               return m_scope.count(name);
                           });
        }
        unfolded[i] = undecided;
    }
    for (std::size_t i = stmt.branches.size(); i-- > 0;) {
        const auto& branch = stmt.branches[i];
        // A static condition decides its branch before it is lowered: a branch that cannot
        // run may hold a null slice, which is legal but has no model form (IEEE 1076-2008
        // 8.5), or a conversion the model cannot represent
        bool staticValue = false;
        const bool isStatic = branch.first && evalCond(*branch.first, staticValue);
        restoreVarVals(entryVals);
        m_unfoldedBranch += unfolded[i];
        AstNodeStmt* bodyp = (isStatic && !staticValue) ? nullptr : lowerStmtList(branch.second);
        m_unfoldedBranch -= unfolded[i];
        if (!branch.first) {
            elsesp = bodyp;
            continue;
        }
        if (isStatic) {
            if (staticValue) {
                if (elsesp) elsesp->deleteTree();
                elsesp = bodyp;
            } else if (bodyp) {
                bodyp->deleteTree();
            }
            continue;
        }
        AstNodeExpr* const condp = lowerCond(*branch.first);
        if (!condp) {
            if (bodyp) bodyp->deleteTree();
            continue;
        }
        const AstConst* const constp = VN_CAST(condp, Const);
        if (constp && constp->num().width() == 1 && !constp->num().isFourState()) {
            const bool taken = constp->num().toUInt() != 0;
            condp->deleteTree();
            if (taken) {
                if (elsesp) elsesp->deleteTree();
                elsesp = bodyp;
            } else if (bodyp) {
                bodyp->deleteTree();
            }
            continue;
        }
        elsesp = new AstIf{stmt.flp, condp, bodyp, elsesp};
    }
    return elsesp;
}

AstNodeStmt* Lowering::lowerStmt(const Stmt& stmt) {
    if (stmt.kind == Stmt::ASSIGN) return lowerAssign(stmt, false);
    if (stmt.kind == Stmt::VAR_ASSIGN) return lowerAssign(stmt, false, true);
    if (stmt.kind == Stmt::IF) return lowerIf(stmt);
    if (stmt.kind == Stmt::CASE) return lowerCase(stmt);
    if (stmt.kind == Stmt::REPORT) return lowerReport(stmt);
    if (stmt.kind == Stmt::ASSERT) return lowerAssert(stmt);
    if (stmt.kind == Stmt::CALL) {
        // `finish` and `stop` of std.env end the simulation, with or without a status
        const Expr& callee = stmt.value->kind == Expr::INDEX && stmt.value->a ? *stmt.value->a
                                                                              : *stmt.value;
        if ((callee.text == "finish" || callee.text == "stop")
            && !m_subprograms.count(callee.text)) {
            if (callee.text == "finish") return new AstFinish{stmt.flp};
            return new AstStop{stmt.flp, false};
        }
        return lowerProcedureCall(*stmt.value, m_concurrentProc > 0);
    }
    if (stmt.kind == Stmt::RETURN) {
        unsupportedError(stmt.flp, "return outside a subprogram body");
        return nullptr;
    }
    if (stmt.kind == Stmt::FOR) return lowerFor(stmt);
    if (stmt.kind == Stmt::WHILE || stmt.kind == Stmt::LOOP) return lowerWhile(stmt);
    if (stmt.kind == Stmt::WAIT) {
        // A bare wait reached at run time suspends the process for good (IEEE
        // 1076-2008 10.2); any other wait inside a body has no model here
        if (stmt.waitForever && m_suspendFlagp) {
            return new AstAssign{stmt.flp, new AstVarRef{stmt.flp, m_suspendFlagp, VAccess::WRITE},
                                 bitConst(stmt.flp, '1')};
        }
        if (m_seqWait) return lowerSequentialWait(stmt);
        unsupportedError(stmt.flp, "wait statement inside a process body");
        return nullptr;
    }
    // exit/next are handled by lowerStmtList, which owns their flags
    return nullptr;
}

AstNodeStmt* Lowering::lowerCase(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    // As in an if, an alternative starts from the values that held before the statement
    const auto entryVals = saveVarVals();
    AstNodeExpr* const selectorp = lowerExpr(*stmt.selector, nullptr);
    if (!selectorp) return nullptr;
    AstNodeDType* const selDtp = selectorp->dtypep();
    VType selType;
    if (!actualType(*stmt.selector, selType)) selType = typeOfDType(selDtp);
    if (!discreteSelectorType(selType)) {
        flp->v3error("VHDL: case selector is not discrete (IEEE 1076-2008 10.9).");
        selectorp->deleteTree();
        return nullptr;
    }
    if (stmt.caseItems.empty()) {
        flp->v3error("VHDL: case statement has no alternatives.");
        selectorp->deleteTree();
        return nullptr;
    }
    AstCaseItem* itemsp = nullptr;
    AstCaseItem* currentp = nullptr;  // The "others" alternative, if seen
    std::set<int64_t> covered;
    bool bad = false;
    for (const auto& item : stmt.caseItems) {
        restoreVarVals(entryVals);
        AstNodeStmt* const bodyp = lowerStmtList(item.second);
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(item.first, selDtp, cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            if (bodyp) bodyp->deleteTree();
            if (itemsp) itemsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        if (cops.empty() && currentp) {
            // A second "others" alternative can never be selected (IEEE 1076-2008 10.9)
            flp->v3error("VHDL: more than one 'others' choice in a case statement.");
            bad = true;
            if (bodyp) bodyp->deleteTree();
            continue;
        }
        if (!addChoiceValues(cops, covered, "case statement")) bad = true;
        AstCaseItem* const caseItemp = cops.empty() ? new AstCaseItem{flp, nullptr, bodyp}
                                                    : new AstCaseItem{flp, cops.front(), bodyp};
        for (size_t i = 1; i < cops.size(); ++i) caseItemp->addCondsp(cops[i]);
        if (cops.empty()) currentp = caseItemp;
        itemsp
            = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, caseItemp) : caseItemp;
    }
    if (!itemsp) {
        selectorp->deleteTree();
        return nullptr;
    }
    if (!bad && !currentp && !choicesComplete(selDtp, selType, covered)) {
        flp->v3error("VHDL: case statement does not cover every value of its selector "
                     "(IEEE 1076-2008 10.9).");
        bad = true;
    }
    if (bad) {
        itemsp->deleteTree();
        selectorp->deleteTree();
        return nullptr;
    }
    return makeCase(flp, selectorp, itemsp);
}

AstNodeStmt* Lowering::lowerStmtList(const std::vector<StmtP>& stmts) {
    return lowerStmtRange(stmts, 0, stmts.size());
}

AstNodeStmt* Lowering::lowerStmtRange(const std::vector<StmtP>& stmts, std::size_t begin,
                                      std::size_t end) {
    // A statement list is a sink, so a local array of an inlined function becomes a
    // variable here rather than a rebuilt expression
    const bool savedMaterialize = m_materializeLocals;
    m_materializeLocals = true;
    AstNodeStmt* headp = nullptr;
    // Conditions that must hold for the following statements to run, set by exit/next
    AstNodeExpr* runCondp = nullptr;
    for (std::size_t index = begin; index < end; ++index) {
        const StmtP& stmtp = stmts[index];
        if (stmtp->kind == Stmt::EXIT || stmtp->kind == Stmt::NEXT) {
            const bool isExit = stmtp->kind == Stmt::EXIT;
            LoopFrame* const framep = findLoop(stmtp->loopLabel);
            if (!framep) {
                unsupportedError(stmtp->flp,
                                 isExit ? "exit outside a loop" : "next outside a loop");
                continue;
            }
            AstVar* const flagp = isExit ? framep->exitp : framep->nextp;
            AstNodeExpr* condp = nullptr;
            if (stmtp->value) {
                condp = lowerCond(*stmtp->value);
                if (!condp) continue;
            }
            AstNodeStmt* const setp = setLoopFlag(stmtp->flp, flagp, condp);
            if (setp)
                headp = headp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, setp) : setp;
            if (!condp) break;  // Unconditional: the rest of this body is unreachable
            AstNodeExpr* const clearp
                = new AstNot{stmtp->flp, new AstVarRef{stmtp->flp, flagp, VAccess::READ}};
            clearp->dtypep(findBasic(stmtp->flp, VBasicDTypeKwd::LOGIC));
            if (runCondp) {
                AstNodeExpr* const andp = new AstAnd{stmtp->flp, runCondp, clearp};
                andp->dtypep(clearp->dtypep());
                runCondp = andp;
            } else {
                runCondp = clearp;
            }
            continue;
        }
        AstNodeStmt* const stmtNodep = lowerStmt(*stmtp);
        // Statements the call's materialized locals produced run before this statement
        AstNodeStmt* const pendingp = takePendingStmts();
        AstNodeStmt* nodep = stmtNodep;
        if (pendingp) {
            nodep = stmtNodep ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(pendingp, stmtNodep)
                              : pendingp;
        }
        updateVarVals(*stmtp);
        if (!nodep) continue;
        // Statements after a conditional exit/next only run when it was not taken
        if (runCondp) nodep = new AstIf{stmtp->flp, runCondp->cloneTree(false), nodep, nullptr};
        if (AstNodeExpr* const guardp = loopGuard(stmtp->flp)) {
            nodep = new AstIf{stmtp->flp, guardp, nodep, nullptr};
        }
        headp = headp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, nodep) : nodep;
        // A wait suspends the process at once: the rest of this list never runs
        if (stmtp->kind == Stmt::WAIT && !m_seqWait) break;
        // Severity failure ends the run at the statement (IEEE 1076-2008 10.3, 10.4):
        // nothing after an unconditional report runs, and the statements after a
        // failing assert run only when it did not fail
        if (stmtp->severity != Stmt::SEV_FAILURE) continue;
        if (stmtp->kind == Stmt::REPORT) break;
        // The assertion's condition cannot be tested again once a wait has passed time
        if (stmtp->kind != Stmt::ASSERT || m_seqWait) continue;
        // The assertion's own If carries the failure condition; `nodep` may already be
        // wrapped by a preceding run condition or loop guard
        const AstIf* const assertp = VN_CAST(stmtNodep, If);
        if (!assertp) continue;
        AstNodeExpr* const passedp = new AstNot{stmtp->flp, assertp->condp()->cloneTree(false)};
        passedp->dtypep(assertp->condp()->dtypep());
        if (runCondp) {
            AstNodeExpr* const andp = new AstAnd{stmtp->flp, runCondp, passedp};
            andp->dtypep(passedp->dtypep());
            runCondp = andp;
        } else {
            runCondp = passedp;
        }
    }
    if (runCondp) runCondp->deleteTree();
    m_materializeLocals = savedMaterialize;
    return headp;
}

//! Innermost active loop, or the one with the given label
Lowering::LoopFrame* Lowering::findLoop(const std::string& label) {
    for (size_t i = m_loops.size(); i-- > 0;) {
        if (label.empty() || m_loops[i].label == label) return &m_loops[i];
    }
    return nullptr;
}

AstVar* Lowering::newLoopFlag(FileLine* flp, const char* what) {
    const std::string name = std::string{"__Vloop_"} + std::to_string(++m_loopSeq) + "__" + what;
    AstVar* const varp
        = new AstVar{flp, VVarType::VAR, name, findBasic(flp, VBasicDTypeKwd::LOGIC)};
    m_modp->addStmtsp(varp);
    return varp;
}

//! `if (cond) flag = 1`, or `flag = 1` when there is no condition
AstNodeStmt* Lowering::setLoopFlag(FileLine* flp, AstVar* flagp, AstNodeExpr* condp) {
    AstNodeExpr* const lhsp = new AstVarRef{flp, flagp, VAccess::WRITE};
    AstNodeExpr* const rhsp = bitConst(flp, '1');
    AstAssign* const assignp = new AstAssign{flp, lhsp, rhsp};
    if (!condp) return assignp;
    return new AstIf{flp, condp, assignp, nullptr};
}

//! Clear a loop flag before it is tested again
AstNodeStmt* Lowering::clearLoopFlag(FileLine* flp, AstVar* flagp) {
    AstNodeExpr* const lhsp = new AstVarRef{flp, flagp, VAccess::WRITE};
    AstNodeExpr* const rhsp = bitConst(flp, '0');
    return new AstAssign{flp, lhsp, rhsp};
}

//! Whether any expression in a statement list calls rising_edge or falling_edge
bool Lowering::stmtsContainEdge(const std::vector<StmtP>& stmts) const {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        for (const ExprP* const exprp :
             {&stmtp->target, &stmtp->value, &stmtp->selector, &stmtp->b, &stmtp->c}) {
            if (*exprp && containsEdgeCall(**exprp)) return true;
        }
        for (const auto& branch : stmtp->branches) {
            if (branch.first && containsEdgeCall(*branch.first)) return true;
            if (stmtsContainEdge(branch.second)) return true;
        }
        for (const auto& item : stmtp->caseItems) {
            if (stmtsContainEdge(item.second)) return true;
        }
        if (stmtsContainEdge(stmtp->body)) return true;
    }
    return false;
}

//! Collect the names a statement list reads; assignment targets are written, not read
void Lowering::collectExprNames(const Expr& expr, std::set<std::string>& names) {
    if (expr.kind == Expr::NAME) {
        names.insert(expr.text);
        return;
    }
    for (const ExprP& childp : expr.args) {
        if (childp) collectExprNames(*childp, names);
    }
    if (expr.a) collectExprNames(*expr.a, names);
    if (expr.b) collectExprNames(*expr.b, names);
    if (expr.c) collectExprNames(*expr.c, names);
    for (const std::vector<ExprP>& choiceList : expr.choices) {
        for (const ExprP& choicep : choiceList) {
            if (choicep) collectExprNames(*choicep, names);
        }
    }
}

void Lowering::collectTargetReadNames(const Expr& expr, std::set<std::string>& names) {
    if (expr.kind == Expr::INDEX || expr.kind == Expr::SLICE) {
        if (expr.kind == Expr::SLICE) {
            if (expr.b) collectExprNames(*expr.b, names);
            if (expr.c) collectExprNames(*expr.c, names);
        }
        for (const ExprP& argp : expr.args) {
            if (argp) collectExprNames(*argp, names);
        }
        if (expr.a) collectTargetReadNames(*expr.a, names);
    } else if (expr.kind == Expr::AGG_POSITIONAL || expr.kind == Expr::AGG_NAMED) {
        for (const std::vector<ExprP>& choiceList : expr.choices) {
            for (const ExprP& choicep : choiceList) {
                if (choicep) collectExprNames(*choicep, names);
            }
        }
    }
}

void Lowering::collectReadNames(const std::vector<StmtP>& stmts, std::set<std::string>& names) {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        if (stmtp->value) collectExprNames(*stmtp->value, names);
        if (stmtp->selector) collectExprNames(*stmtp->selector, names);
        if (stmtp->b) collectExprNames(*stmtp->b, names);
        if (stmtp->c) collectExprNames(*stmtp->c, names);
        if (stmtp->target) collectTargetReadNames(*stmtp->target, names);
        for (const auto& branch : stmtp->branches) {
            if (branch.first) collectExprNames(*branch.first, names);
            collectReadNames(branch.second, names);
        }
        for (const auto& item : stmtp->caseItems) {
            for (const ExprP& choicep : item.first) {
                if (choicep) collectExprNames(*choicep, names);
            }
            collectReadNames(item.second, names);
        }
        std::set<std::string> bodyNames;
        collectReadNames(stmtp->body, bodyNames);
        if (stmtp->kind == Stmt::FOR) bodyNames.erase(stmtp->loopVar);
        names.insert(bodyNames.begin(), bodyNames.end());
    }
}

//! Whether a statement list (recursively) contains an exit or next
bool Lowering::usesExitNext(const std::vector<StmtP>& stmts) {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        if (stmtp->kind == Stmt::EXIT || stmtp->kind == Stmt::NEXT) return true;
        for (const auto& branch : stmtp->branches) {
            if (usesExitNext(branch.second)) return true;
        }
        for (const auto& item : stmtp->caseItems) {
            if (usesExitNext(item.second)) return true;
        }
        if (usesExitNext(stmtp->body)) return true;
    }
    return false;
}

//! Whether a statement list (recursively) contains a bare `wait;`
bool Lowering::usesBareWait(const std::vector<StmtP>& stmts) {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        if (stmtp->kind == Stmt::WAIT && stmtp->waitForever) return true;
        for (const auto& branch : stmtp->branches) {
            if (usesBareWait(branch.second)) return true;
        }
        for (const auto& item : stmtp->caseItems) {
            if (usesBareWait(item.second)) return true;
        }
        if (usesBareWait(stmtp->body)) return true;
    }
    return false;
}

//! Whether a statement list (recursively) ends the run: a report or assert of
//! severity failure (IEEE 1076-2008 10.3, 10.4)
bool Lowering::usesSeverityFailure(const std::vector<StmtP>& stmts) {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        if ((stmtp->kind == Stmt::REPORT || stmtp->kind == Stmt::ASSERT)
            && stmtp->severity == Stmt::SEV_FAILURE) {
            return true;
        }
        for (const auto& branch : stmtp->branches) {
            if (usesSeverityFailure(branch.second)) return true;
        }
        for (const auto& item : stmtp->caseItems) {
            if (usesSeverityFailure(item.second)) return true;
        }
        if (usesSeverityFailure(stmtp->body)) return true;
    }
    return false;
}

//! Condition under which the rest of the enclosing loops may still run
AstNodeExpr* Lowering::loopGuard(FileLine* flp) const {
    AstNodeExpr* guardp = nullptr;
    for (const LoopFrame& frame : m_loops) {
        if (!frame.guard) continue;
        for (AstVar* const flagp : {frame.exitp, frame.nextp}) {
            AstNodeExpr* const notp = new AstNot{flp, new AstVarRef{flp, flagp, VAccess::READ}};
            notp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            if (!guardp) {
                guardp = notp;
                continue;
            }
            AstNodeExpr* const andp = new AstAnd{flp, guardp, notp};
            andp->dtypep(notp->dtypep());
            guardp = andp;
        }
    }
    return guardp;
}

//! Lower a process that suspends on a wait statement instead of a sensitivity list
void Lowering::lowerWaitProcess(const Process& process) {
    FileLine* const flp = process.flp;
    const ScopeGuard scopeGuard{*this};
    std::set<std::string> varNames;
    emitProcessDecls(process, varNames);
    WaitProcessLowering ctx;
    ctx.processp = &process;
    ctx.flp = flp;
    ctx.waitIndex = process.body.size();
    ctx.stopIndex = process.body.size();
    if (needsSequentialWaits(process)) {
        lowerSequentialWaitProcess(process);
        return;
    }
    ctx.waitp = findProcessWait(process, ctx.waitIndex);
    if (!ctx.waitp) {
        lowerWaitWithoutWait(process, flp);
        return;
    }
    if (!findTrailingWait(process, ctx)) return;
    const std::vector<StmtP>& bodyStmts = process.body;
    // A bare `wait;` in the body, other than the process's own first wait, suspends
    // the process when reached (IEEE 1076-2008 10.2): a flag gates every later
    // activation, and the reached wait sets it
    m_suspendFlagp = nullptr;
    ctx.bareWaitInBody = !ctx.waitp->waitForever && usesBareWait(bodyStmts);
    if (ctx.waitp->delay) {
        lowerTimedWaitProcess(ctx);
        return;
    }
    // The statements before the wait run once at time zero
    m_initial = true;
    AstNodeStmt* const preInitp = lowerStmtRange(bodyStmts, 0, ctx.waitIndex);
    m_initial = false;
    if (preInitp) m_modp->addStmtsp(new AstInitialStatic{flp, preInitp});
    if (ctx.waitp->waitForever) {
        // A bare wait suspends the process forever
        return;
    }
    if (!ctx.waitp->value) {
        unsupportedError(flp, "wait on a sensitivity list");
        return;
    }
    m_edges.clear();
    AstNodeExpr* const condp = lowerCond(*ctx.waitp->value);
    if (!condp) return;
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    AstSenItem* const itemsp = makeWaitSensitivity(*ctx.waitp->value, edges, flp);
    if (!itemsp) {
        unsupportedError(flp, "wait statement without a sensitivity item");
        condp->deleteTree();
        return;
    }
    // Each activation runs the statements after the wait, then those before it
    m_initial = false;
    if (ctx.bareWaitInBody && !ctx.singleShot) makeWaitSuspendFlag(flp);
    AstNodeStmt* const bodyp = makeWaitActivationBody(ctx, condp);
    if (bodyp) {
        m_modp->addStmtsp(
            new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemsp}, bodyp});
    } else {
        itemsp->deleteTree();
    }
    m_suspendFlagp = nullptr;
}

//! First wait in a process body, whose surrounding statements bracket each activation
const Stmt* Lowering::findProcessWait(const Process& process, std::size_t& waitIndex) const {
    // Find the first wait; statements around it run at time zero and per activation
    for (std::size_t i = 0; i < process.body.size(); ++i) {
        if (process.body[i] && process.body[i]->kind == Stmt::WAIT) {
            waitIndex = i;
            return process.body[i].get();
        }
    }
    return nullptr;
}

//! Lower a process with no wait, which runs once unless it ends the run itself
void Lowering::lowerWaitWithoutWait(const Process& process, FileLine* flp) {
    // A severity-failure report or assert, when the process reaches it, ends
    // the run (IEEE 1076-2008 10.3, 10.4), which is a valid process stopper: the body
    // runs once, at time zero, and the run ends at the failure
    if (!usesSeverityFailure(process.body)) {
        unsupportedError(flp, "process without a sensitivity list or wait statement");
        return;
    }
    m_initial = true;
    m_varVals.clear();
    AstNodeStmt* const initp = lowerStmtList(process.body);
    m_initial = false;
    if (initp) m_modp->addStmtsp(new AstInitialStatic{flp, initp});
}

//! Find a trailing bare wait, which makes the process single-shot
bool Lowering::findTrailingWait(const Process& process, WaitProcessLowering& ctx) const {
    // A trailing bare wait makes the process single-shot: it suspends forever after
    // the first activation
    for (std::size_t i = ctx.waitIndex + 1; i < process.body.size(); ++i) {
        if (!process.body[i] || process.body[i]->kind != Stmt::WAIT) continue;
        const Stmt* const laterp = process.body[i].get();
        const bool last = i + 1 == process.body.size();
        if (!last || !laterp->waitForever) {
            unsupportedError(ctx.flp, "process with more than one wait statement");
            return false;
        }
        ctx.singleShot = true;
        ctx.stopIndex = i;
        break;
    }
    return true;
}

//! Count the waits of a statement list that are not a bare `wait;`, in nested statements and
//! in the bodies of the procedures it calls. `seen` stops a recursive procedure
void Lowering::countTimedWaits(const std::vector<StmtP>& stmts,
                               std::set<const SubprogramDecl*>& seen, std::size_t& count) const {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        if (stmtp->kind == Stmt::WAIT && !stmtp->waitForever) ++count;
        if (stmtp->kind == Stmt::CALL) {
            const Expr& callExpr = *stmtp->value;
            const std::string name
                = callExpr.kind == Expr::INDEX && callExpr.a ? callExpr.a->text : callExpr.text;
            const auto it = m_subprograms.find(name);
            if (it == m_subprograms.end()) continue;
            for (const SubprogramRef& ref : it->second) {
                if (ref.declp && ref.declp->hasBody && seen.insert(ref.declp).second)
                    countTimedWaits(ref.declp->body, seen, count);
            }
        }
        for (const auto& branch : stmtp->branches) countTimedWaits(branch.second, seen, count);
        for (const auto& item : stmtp->caseItems) countTimedWaits(item.second, seen, count);
        countTimedWaits(stmtp->body, seen, count);
    }
}

//! Whether a process has waits that need to suspend it in the middle of its body
bool Lowering::needsSequentialWaits(const Process& process) {
    std::set<const SubprogramDecl*> seen;
    std::size_t count = 0;
    countTimedWaits(process.body, seen, count);
    if (count > 1) return true;
    if (count == 0) return false;
    // One wait is handled by the activation scheme only at the top level of the body
    return std::none_of(process.body.begin(), process.body.end(), [](const StmtP& stmtp) {
        return stmtp && stmtp->kind == Stmt::WAIT && !stmtp->waitForever;
    });
}

//! Lower a process with several waits as one timed block that repeats its whole body
void Lowering::lowerSequentialWaitProcess(const Process& process) {
    FileLine* const flp = process.flp;
    m_suspendFlagp = nullptr;
    m_seqWait = true;
    AstNodeStmt* const bodyp = lowerStmtList(process.body);
    m_seqWait = false;
    if (!bodyp) return;
    AstLoop* const loopp = new AstLoop{flp};
    loopp->addStmtsp(new AstLoopTest{flp, loopp, bitConst(flp, '1')});
    loopp->addStmtsp(bodyp);
    m_modp->addStmtsp(new AstInitial{flp, loopp});
}

//! Lower one wait of a process with several waits to the timing control it stands for
AstNodeStmt* Lowering::lowerSequentialWait(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    if (stmt.waitForever) return new AstWait{flp, bitConst(flp, '0'), nullptr};
    if (stmt.delay) return lowerDelay(*stmt.delay, flp);
    AstSenItem* itemsp = nullptr;
    AstNodeExpr* condp = nullptr;
    if (stmt.value) {
        m_edges.clear();
        condp = lowerCond(*stmt.value);
        const std::vector<Edge> edges = m_edges;
        m_edges.clear();
        if (!condp) return nullptr;
        itemsp = makeWaitSensitivity(*stmt.value, edges, flp, true);
        if (!itemsp) {
            unsupportedError(flp, "wait statement without a sensitivity item");
            condp->deleteTree();
            return nullptr;
        }
    } else {
        for (const std::string& name : stmt.waitNames) {
            AstVar* const varp = visibleVar(name);
            if (!varp) {
                unsupportedError(flp, "wait on '" + name + "', which is not a signal");
                if (itemsp) itemsp->deleteTree();
                return nullptr;
            }
            AstSenItem* const itemp = new AstSenItem{flp, VEdgeType::ET_CHANGED,
                                                     new AstVarRef{flp, varp, VAccess::READ}};
            itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
        }
    }
    AstNodeStmt* const eventp = new AstEventControl{flp, new AstSenTree{flp, itemsp}, nullptr};
    if (!condp) return eventp;
    // `wait until` resumes at the first event after which the condition holds
    AstLoop* const loopp = new AstLoop{flp};
    loopp->addStmtsp(eventp);
    AstNodeExpr* const notp = new AstNot{flp, condp};
    notp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    loopp->addStmtsp(new AstLoopTest{flp, loopp, notp});
    return loopp;
}

//! Create the flag a reached bare wait sets to gate later activations
void Lowering::makeWaitSuspendFlag(FileLine* flp) {
    m_suspendFlagp
        = new AstVar{flp, VVarType::VAR, "__Vwait_" + std::to_string(++m_loopSeq) + "__done",
                     findBasic(flp, VBasicDTypeKwd::LOGIC)};
    m_modp->addStmtsp(m_suspendFlagp);
}

//! Lower a process whose first wait is a delay, repeating its whole body each time
void Lowering::lowerTimedWaitProcess(const WaitProcessLowering& ctx) {
    FileLine* const flp = ctx.flp;
    const Process& process = *ctx.processp;
    if (ctx.bareWaitInBody) makeWaitSuspendFlag(flp);
    // A timed wait suspends the process until the delay expires and the process then
    // runs again from the top, so its whole body repeats
    AstNodeStmt* bodyp = lowerStmtRange(process.body, 0, ctx.waitIndex);
    AstNodeStmt* const delayp = lowerDelay(*ctx.waitp->delay, flp);
    if (!delayp) {
        m_suspendFlagp = nullptr;
        return;
    }
    bodyp = bodyp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, delayp) : delayp;
    if (AstNodeStmt* const postp
        = lowerStmtRange(process.body, ctx.waitIndex + 1, process.body.size())) {
        bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, postp);
    }
    AstLoop* const loopp = new AstLoop{flp};
    // The repeated body stops for good when it reaches a bare wait
    AstNodeExpr* runp = bitConst(flp, '1');
    if (m_suspendFlagp) {
        runp = new AstNot{flp, new AstVarRef{flp, m_suspendFlagp, VAccess::READ}};
        runp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    }
    loopp->addStmtsp(new AstLoopTest{flp, loopp, runp});
    if (bodyp) loopp->addStmtsp(bodyp);
    m_modp->addStmtsp(new AstInitial{flp, loopp});
    m_suspendFlagp = nullptr;
}

//! Sensitivity of a wait condition: its named edges and the levels it reads
AstSenItem* Lowering::makeWaitSensitivity(const Expr& waitValue, const std::vector<Edge>& edges,
                                          FileLine* flp, bool edgesOnly) const {
    // Sensitivity: the clock edges the condition names, plus the levels it tests
    AstSenItem* itemsp = nullptr;
    for (const Edge& edge : edges) {
        AstSenItem* const itemp
            = new AstSenItem{flp, edge.type, new AstVarRef{flp, edge.varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    // A condition that names an edge only changes with that edge
    if (edgesOnly && itemsp) return itemsp;
    std::set<std::string> levelNames;
    collectExprNames(waitValue, levelNames);
    for (const std::string& name : levelNames) {
        AstVar* const varp = visibleVar(name);
        if (!varp) continue;
        if (std::any_of(edges.begin(), edges.end(),
                        [&](const Edge& edge) { return edge.varp == varp; })) {
            continue;  // Already an edge item
        }
        AstSenItem* const itemp
            = new AstSenItem{flp, VEdgeType::ET_CHANGED, new AstVarRef{flp, varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    return itemsp;
}

AstVar* Lowering::visibleVar(const std::string& name) const {
    const auto bindingIt = activeEnv().find(name);
    if (bindingIt != activeEnv().end()) {
        const AstVarRef* const refp = VN_CAST(bindingIt->second.exprp, VarRef);
        return refp ? refp->varp() : nullptr;
    }
    const auto it = m_scope.find(name);
    return it == m_scope.end() ? nullptr : it->second.varp;
}

//! Body run on each activation, with single-shot and bare-wait gating applied
AstNodeStmt* Lowering::makeWaitActivationBody(const WaitProcessLowering& ctx, AstNodeExpr* condp) {
    FileLine* const flp = ctx.flp;
    const Process& process = *ctx.processp;
    const std::vector<StmtP>& bodyStmts = process.body;
    AstNodeStmt* const afterp = lowerStmtRange(bodyStmts, ctx.waitIndex + 1, ctx.stopIndex);
    AstNodeStmt* const preActp
        = ctx.singleShot ? nullptr : lowerStmtRange(bodyStmts, 0, ctx.waitIndex);
    AstNodeStmt* bodyp = afterp;
    if (preActp) {
        bodyp = bodyp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, preActp) : preActp;
    }
    if (ctx.singleShot) {
        // Latch that the single activation has happened
        const std::string name = "__Vloop_" + std::to_string(++m_loopSeq) + "__once";
        AstVar* const oncep
            = new AstVar{flp, VVarType::VAR, name, findBasic(flp, VBasicDTypeKwd::LOGIC)};
        m_modp->addStmtsp(oncep);
        AstNodeExpr* const onceReadp = new AstVarRef{flp, oncep, VAccess::READ};
        AstNodeExpr* const notOncep = new AstNot{flp, onceReadp};
        notOncep->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        AstNodeStmt* innerp = nullptr;
        if (bodyp) {
            AstNodeStmt* const setp = new AstAssign{flp, new AstVarRef{flp, oncep, VAccess::WRITE},
                                                    bitConst(flp, '1')};
            innerp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(setp, bodyp);
        } else {
            innerp = new AstAssign{flp, new AstVarRef{flp, oncep, VAccess::WRITE},
                                   bitConst(flp, '1')};
        }
        AstNodeStmt* const gatedp = new AstIf{flp, condp, innerp, nullptr};
        bodyp = new AstIf{flp, notOncep, gatedp, nullptr};
    } else if (m_suspendFlagp && bodyp) {
        // A reached bare wait set the flag: every later activation is gated off
        AstNodeExpr* const notDonep
            = new AstNot{flp, new AstVarRef{flp, m_suspendFlagp, VAccess::READ}};
        notDonep->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        bodyp = new AstIf{flp, condp, new AstIf{flp, notDonep, bodyp, nullptr}, nullptr};
    } else if (bodyp) {
        bodyp = new AstIf{flp, condp, bodyp, nullptr};
    } else {
        condp->deleteTree();
    }
    return bodyp;
}

AstNodeStmt* Lowering::lowerFor(const Stmt& stmt) {
    m_varVals.clear();
    FileLine* const flp = stmt.flp;
    int64_t left = 0;
    int64_t right = 0;
    bool up = stmt.rangeAscending;
    if (!stmt.c) {
        // `for i in subtype loop`: a discrete subtype is its own range
        const auto subtypeIt
            = stmt.b && stmt.b->kind == Expr::NAME ? m_types.find(stmt.b->text) : m_types.end();
        if (subtypeIt != m_types.end() && subtypeIt->second.kind == VType::INT) {
            left = subtypeIt->second.intLow;
            right = subtypeIt->second.intHigh;
            up = left <= right;
        } else {
            // `for i in name'range loop`: the bounds come from the prefix's type
            if (!stmt.b || stmt.b->kind != Expr::ATTR || stmt.b->text != "range" || !stmt.b->a
                || stmt.b->a->kind != Expr::NAME) {
                unsupportedError(flp, "loop range without 'to' or 'downto'");
                return nullptr;
            }
            const VType* const typep = typeOfName(*stmt.b->a);
            if (!typep) {
                unsupportedError(flp, "loop range of an unresolved name");
                return nullptr;
            }
            const VType* const levelp = indexRangeLevel(*stmt.b, *typep);
            if (!levelp) {
                unsupportedError(flp, "loop range of an unknown index");
                return nullptr;
            }
            left = levelp->left;
            right = levelp->right;
            up = levelp->ascending;
        }
    } else if (!staticInt(stmt.b, left) || !staticInt(stmt.c, right)) {
        // A sequential loop evaluates its bounds when it is reached, so they may read a
        // signal: run the loop at run time instead of unrolling it
        return lowerForRuntime(stmt);
    }
    if (up ? right < left : right > left) {
        // An empty range: the loop body never runs
        return nullptr;
    }
    if (std::llabs(right - left) > MAX_FOLD_ITERATIONS) {
        // Unrolling a long range would bloat the netlist, so it runs as a loop
        if (stmt.c) return lowerForRuntime(stmt);
        unsupportedError(flp, "loop range with more than " + std::to_string(MAX_FOLD_ITERATIONS)
                                  + " iterations");
        return nullptr;
    }
    LoopFrame frame;
    frame.label = stmt.loopLabel;
    frame.exitp = newLoopFlag(flp, "exit");
    frame.nextp = newLoopFlag(flp, "next");
    frame.guard = usesExitNext(stmt.body);
    m_loops.push_back(frame);
    AstNodeStmt* headp = clearLoopFlag(flp, frame.exitp);
    AstNodeStmt* const initp = clearLoopFlag(flp, frame.nextp);
    headp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, initp);

    // The loop variable is a static constant, so each iteration can be unrolled
    // An inline binding of the loop variable's name, e.g. a procedure formal, must not
    // shadow the iteration constant
    const InlineEnv& bindings = activeEnv();
    const auto boundIt = bindings.find(stmt.loopVar);
    std::unique_ptr<InlineBinding> boundSavep
        = boundIt != bindings.end() ? std::make_unique<InlineBinding>(boundIt->second) : nullptr;
    activeEnv().erase(stmt.loopVar);
    const auto savedConst = m_consts.find(stmt.loopVar);
    const bool hadConst = savedConst != m_consts.end();
    const int64_t savedValue = hadConst ? savedConst->second : 0;
    AstNodeExpr* const exitNotp = [&]() -> AstNodeExpr* {
        AstNodeExpr* const notp = new AstNot{flp, new AstVarRef{flp, frame.exitp, VAccess::READ}};
        notp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        return notp;
    }();
    for (int64_t value = left;; value += up ? 1 : -1) {
        m_consts[stmt.loopVar] = value;
        // An exit ends every later iteration; a next only resets per iteration
        if (frame.guard) {
            AstNodeStmt* const clearNextp = clearLoopFlag(flp, frame.nextp);
            headp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, clearNextp);
        }
        m_varVals.clear();
        AstNodeStmt* const bodyp = lowerStmtList(stmt.body);
        if (bodyp) {
            AstNodeStmt* const guardp = new AstIf{flp, exitNotp->cloneTree(false), bodyp, nullptr};
            headp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(headp, guardp);
        }
        if (value == right) break;
    }
    exitNotp->deleteTree();
    if (hadConst) {
        m_consts[stmt.loopVar] = savedValue;
    } else {
        m_consts.erase(stmt.loopVar);
    }
    if (boundSavep) activeEnv()[stmt.loopVar] = *boundSavep;
    m_loops.pop_back();
    return headp;
}

AstNodeStmt* Lowering::lowerForRuntime(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    const bool up = stmt.rangeAscending;
    VType intT = intType(flp);
    int64_t left = 0;
    int64_t right = 0;
    AstNodeExpr* const initp
        = staticInt(stmt.b, left) ? makeIntConst(flp, left) : lowerExpr(*stmt.b, &intT);
    AstNodeExpr* boundp
        = staticInt(stmt.c, right) ? makeIntConst(flp, right) : lowerExpr(*stmt.c, &intT);
    if (!initp || !boundp) return nullptr;
    // The loop variable is an ordinary variable: the body reads and writes it through the
    // scope, so its references lower to variable references rather than to constants
    AstVar* const varp
        = new AstVar{flp, VVarType::VAR, "__Vfor_" + std::to_string(++m_loopSeq), intT.dtp};
    m_modp->addStmtsp(varp);
    const ScopeSave savedScope = saveScope();
    m_scope.erase(stmt.loopVar);
    m_scope[stmt.loopVar] = VarInfo{varp, intT};
    // The discrete range is evaluated once, at loop entry (IEEE 1076-2008 10.10), so a
    // bound the body writes must not re-read on every test: capture it first
    AstNodeStmt* capturep = nullptr;
    if (!VN_CAST(boundp, Const)) {
        AstVar* const boundVarp
            = new AstVar{flp, VVarType::VAR, "__Vbound_" + std::to_string(++m_loopSeq), intT.dtp};
        m_modp->addStmtsp(boundVarp);
        capturep = new AstAssign{flp, new AstVarRef{flp, boundVarp, VAccess::WRITE}, boundp};
        boundp = new AstVarRef{flp, boundVarp, VAccess::READ};
    }
    // `exit` and `next` latch the same per-loop flags the unrolled path uses: the exit
    // flag joins the loop test, so the loop stops after the iteration that set it, and
    // the statements after an exit or next are guarded by their flag
    LoopFrame frame;
    frame.label = stmt.loopLabel;
    frame.exitp = newLoopFlag(flp, "exit");
    frame.nextp = newLoopFlag(flp, "next");
    frame.guard = usesExitNext(stmt.body);
    m_loops.push_back(frame);
    AstLoop* const loopp = new AstLoop{flp};
    AstNodeExpr* const rangep
        = up ? static_cast<AstNodeExpr*>(
                   new AstLte{flp, new AstVarRef{flp, varp, VAccess::READ}, boundp})
             : static_cast<AstNodeExpr*>(
                   new AstGte{flp, new AstVarRef{flp, varp, VAccess::READ}, boundp});
    rangep->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    AstNodeExpr* const exitNotp = new AstNot{flp, new AstVarRef{flp, frame.exitp, VAccess::READ}};
    exitNotp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    AstNodeExpr* const testp = new AstAnd{flp, rangep, exitNotp};
    testp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    loopp->addStmtsp(new AstLoopTest{flp, loopp, testp});
    // The next flag only skips the rest of one iteration
    if (frame.guard) loopp->addStmtsp(clearLoopFlag(flp, frame.nextp));
    AstNodeStmt* const bodyp = lowerStmtList(stmt.body);
    m_loops.pop_back();
    if (bodyp) loopp->addStmtsp(bodyp);
    AstNodeExpr* const stepp
        = up ? static_cast<AstNodeExpr*>(
                   new AstAdd{flp, new AstVarRef{flp, varp, VAccess::READ}, new AstConst{flp, 1}})
             : static_cast<AstNodeExpr*>(
                   new AstSub{flp, new AstVarRef{flp, varp, VAccess::READ}, new AstConst{flp, 1}});
    stepp->dtypep(intT.dtp);
    loopp->addStmtsp(new AstAssign{flp, new AstVarRef{flp, varp, VAccess::WRITE}, stepp});
    restoreScope(savedScope);
    AstNodeStmt* resultp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(
        clearLoopFlag(flp, frame.exitp),
        AstNode::addNext<AstNodeStmt, AstNodeStmt>(
            new AstAssign{flp, new AstVarRef{flp, varp, VAccess::WRITE}, initp}, loopp));
    if (capturep) resultp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(capturep, resultp);
    return resultp;
}

//! The text of a literal report message, or a default when there is none
std::string reportText(const Expr* exprp, const char* fallback) {
    if (!exprp) return fallback;
    if (exprp->kind != Expr::STRING_LIT) return fallback;
    std::string text = exprp->spelling;
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

AstNodeStmt* Lowering::lowerReport(const Stmt& stmt) {
    // A computed message is not modelled, but the statement still executes: dropping it
    // would also drop a `severity failure` termination (IEEE 1076-2008 10.4)
    // `report ... severity failure` fails the run like `$fatal` (IEEE 1076-2008 10.4); the
    // other severities only report
    const bool failure = stmt.severity == Stmt::SEV_FAILURE;
    AstNodeStmt* const displayp
        = new AstDisplay{stmt.flp,
                         failure ? VDisplayType::DT_FATAL : VDisplayType::DT_DISPLAY,
                         reportText(stmt.value.get(), "report"),
                         nullptr,
                         nullptr,
                         'd'};
    if (failure) {
        return AstNode::addNext<AstNodeStmt, AstNodeStmt>(displayp, new AstStop{stmt.flp, true});
    }
    return displayp;
}

AstNodeStmt* Lowering::lowerAssert(const Stmt& stmt) {
    // A concurrent procedure call is already one combinational process, so its
    // assertions run inside it
    return assertBody(stmt.flp, *stmt.value, stmt.message.get(), stmt.severity);
}

AstNodeStmt* Lowering::assertBody(FileLine* flp, const Expr& condition, const Expr* messagep,
                                  Stmt::Severity severity) {
    AstNodeExpr* const condp = lowerCond(condition);
    if (!condp) return nullptr;
    // A failing assertion reports, and `severity failure` fails the run like `$fatal`
    // (IEEE 1076-2008 10.3)
    AstNodeExpr* const failp = new AstNot{flp, condp};
    failp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    const bool failure = severity == Stmt::SEV_FAILURE;
    AstNodeStmt* bodyp
        = new AstDisplay{flp,
                         failure ? VDisplayType::DT_FATAL : VDisplayType::DT_DISPLAY,
                         reportText(messagep, "Assertion failed"),
                         nullptr,
                         nullptr,
                         'd'};
    if (failure) bodyp = AstNode::addNext<AstNodeStmt, AstNodeStmt>(bodyp, new AstStop{flp, true});
    return new AstIf{flp, failp, bodyp, nullptr};
}

AstNodeStmt* Lowering::lowerDelay(const Expr& expr, FileLine* flp) {
    // Like an SV time literal: V3Width scales the seconds to the module's timeunit, which
    // is only known after the frontend has run
    int64_t fs = 0;
    AstNodeExpr* secondsp = nullptr;
    if (evalInt(expr, fs)) {
        secondsp = new AstConst{flp, AstConst::RealDouble{}, static_cast<double>(fs) * 1e-15};
    } else {
        // A run-time delay is a 64-bit count of femtoseconds
        const VType type = timeType(flp);
        AstNodeExpr* const countp = lowerExpr(expr, &type);
        if (!countp) return nullptr;
        secondsp = new AstMulD{
            flp, new AstIToRD{flp, resizeExpr(countp, type, flp)},
            new AstConst{flp, AstConst::RealDouble{}, 1e-15}};
    }
    AstTimeImport* const timep = new AstTimeImport{flp, secondsp};
    timep->dtypeSetDouble();
    return new AstDelay{flp, timep, false};
}

AstNodeStmt* Lowering::lowerWhile(const Stmt& stmt) {
    unsupportedError(stmt.flp, stmt.kind == Stmt::WHILE ? "while loop" : "bare loop");
    return nullptr;
}

//######################################################################
// Subprograms: lookup, function inlining, procedure expansion

}  // namespace vhdlnative
