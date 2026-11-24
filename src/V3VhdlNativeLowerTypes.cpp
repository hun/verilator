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
// Registers VHDL types and folds elaboration-time type and constant expressions
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

//! term is scope-independent. An index callee is not a value reference: the folders only
//! compute the builtin conversions and real functions, so anything else there fails to
//! fold instead of being guessed at.
bool Lowering::closedOverFormals(const Expr& expr,
                                 const std::map<std::string, ExprP>& args) const {
    switch (expr.kind) {
    case Expr::INT_LIT:
    case Expr::PHYS_LIT:
    case Expr::REAL_LIT:
    case Expr::BITS_LIT:
    case Expr::CHAR_LIT:
    case Expr::STRING_LIT: return true;
    case Expr::NAME: return args.count(expr.text) != 0;
    case Expr::PAREN:
    case Expr::UNARY: return expr.a && closedOverFormals(*expr.a, args);
    case Expr::BINARY:
        return expr.a && expr.b && closedOverFormals(*expr.a, args)
               && closedOverFormals(*expr.b, args);
    case Expr::INDEX:
        for (const ExprP& argp : expr.args) {
            if (!argp || !closedOverFormals(*argp, args)) return false;
        }
        return true;
    case Expr::ATTR:
        // An attribute of a type mark is closed over the formals; one of an object is
        // not, because the folders take an object's bounds from the scope
        if (!expr.a || expr.a->kind != Expr::NAME) return false;
        if (args.count(expr.a->text)) return false;
        if (typeOfName(*expr.a)) return false;
        return m_types.count(expr.a->text) || isPredefinedIntegerMark(expr.a->text);
    default: return false;
    }
}

//! Whether an attribute names one index range of an array, which a bracketed index selects:
//! A'LEFT(N), A'RIGHT(N), A'HIGH(N), A'LOW(N), A'RANGE(N), A'REVERSE_RANGE(N), A'LENGTH(N)
//! and A'ASCENDING(N) of IEEE 1076-2008 16.2.3. The attributes that take a *value*, like
//! `T'pos(red)`, are not among them.
bool isIndexRangeAttribute(const std::string& attribute) {
    return attribute == "range" || attribute == "reverse_range" || attribute == "left"
           || attribute == "right" || attribute == "high" || attribute == "low"
           || attribute == "length" || attribute == "ascending";
}

//! The index range an array attribute refers to: one element level per unit above the index
//! it names. Null when the index is not static or exceeds the type's dimensionality.
const VType* Lowering::indexRangeLevel(const Expr& expr, const VType& type) const {
    const VType* levelp = &type;
    if (!isIndexRangeAttribute(expr.text)) return levelp;
    int64_t index = 1;
    if (!expr.args.empty() && (!staticInt(expr.args[0], index) || index < 1)) return nullptr;
    for (int64_t i = 1; i < index; ++i) {
        if (levelp->kind != VType::ARRAY || !levelp->elemp) return nullptr;
        levelp = levelp->elemp.get();
    }
    return levelp;
}

bool Lowering::evalInt(const Expr& expr, int64_t& value) const {
    switch (expr.kind) {
    case Expr::INT_LIT: value = expr.intValue; return true;
    case Expr::PHYS_LIT: return physicalValue(*m_designp, expr, value);
    case Expr::REAL_LIT:
        // An integral real literal folded to an integer value
        if (std::isfinite(expr.realValue) && expr.realValue == std::floor(expr.realValue)) {
            value = static_cast<int64_t>(expr.realValue);
            return true;
        }
        return false;
    case Expr::NAME: return evalIntName(expr, value);
    case Expr::ATTR: return evalIntAttribute(expr, value);
    case Expr::PAREN: return evalInt(*expr.a, value);
    case Expr::UNARY: {
        int64_t operand = 0;
        if (!evalInt(*expr.a, operand)) return false;
        if (expr.text == "+") {
            value = operand;
            return true;
        }
        if (expr.text == "-") {
            value = -operand;
            return true;
        }
        if (expr.text == "abs") {
            value = operand < 0 ? -operand : operand;
            return true;
        }
        return false;
    }
    case Expr::BINARY: return evalIntBinary(expr, value);
    case Expr::INDEX: return evalIntIndex(expr, value);
    default: return false;
    }
}

//! Integer value of a name: a bound pure-call formal, a constant, or a foldable call
bool Lowering::evalIntName(const Expr& expr, int64_t& value) const {
    if (!m_pureCalls.empty()) {
        const std::map<std::string, ExprP>& args = m_pureCalls.back().args;
        const auto argIt = args.find(expr.text);
        if (argIt != args.end()) return evalInt(*argIt->second, value);
    }
    const InlineEnv& bindings = activeEnv();
    const auto bindIt = bindings.find(expr.text);
    if (bindIt != bindings.end() && !isAggregateType(bindIt->second.type)) {
        // A fold is speculative and reports nothing, so a value that does not fit an
        // integer is simply not folded (a string, or more than 64 bits)
        if (const AstConst* const boundp = VN_CAST(bindIt->second.exprp, Const)) {
            if (boundp->num().isFourState() || boundp->width() > 64) return false;
            value = boundp->num().isSigned() ? boundp->num().toSQuad()
                                             : static_cast<int64_t>(boundp->num().toUQuad());
            return true;
        }
    }
    const auto it = m_consts.find(expr.text);
    if (it != m_consts.end()) {
        value = it->second;
        return true;
    }
    const auto preIt = m_preConsts.find(expr.text);
    if (preIt != m_preConsts.end()) {
        value = preIt->second;
        return true;
    }
    if (m_ambiguousLiterals.count(expr.text)) return false;
    const auto enumIt = m_enumConsts.find(expr.text);
    if (enumIt != m_enumConsts.end()) {
        value = static_cast<int64_t>(enumIt->second->num().toUInt());
        return true;
    }
    const auto valueIt = m_constValues.find(expr.text);
    if (valueIt != m_constValues.end()) {
        const auto constTypeIt = m_constTypes.find(expr.text);
        if (constTypeIt != m_constTypes.end()
            && (constTypeIt->second.kind == VType::INT
                || constTypeIt->second.kind == VType::ENUM)) {
            value = static_cast<int64_t>(valueIt->second->num().toUInt());
            return true;
        }
    }
    // A function without a formal may be named without parentheses
    const Expr* returnp = nullptr;
    if (pushPureCall(expr, returnp)) {
        const bool folded = evalInt(*returnp, value);
        popPureCall();
        return folded;
    }
    return false;
}

//! Integer value of an attribute of a type mark, including its position attributes
bool Lowering::evalIntAttribute(const Expr& expr, int64_t& value) const {
    // Attributes of a type mark or name whose value is known statically
    if (!expr.a || expr.a->kind != Expr::NAME) return false;
    const VType* typep = typeOfName(*expr.a);
    VType predefined;
    if (!typep) {
        const auto typeIt = m_types.find(expr.a->text);
        if (typeIt != m_types.end()) {
            typep = &typeIt->second;
        } else if (predefinedIntegerType(expr.flp, expr.a->text, predefined)) {
            typep = &predefined;
        } else {
            return false;
        }
    }
    const std::string& attribute = expr.text;
    // Fold to the index range the attribute names, which a bracketed index selects
    typep = indexRangeLevel(expr, *typep);
    if (!typep) return false;
    if (attribute == "length") {
        value = typep->length();
        return true;
    }
    if (attribute == "left") {
        value = typep->left;
        return true;
    }
    if (attribute == "right") {
        value = typep->right;
        return true;
    }
    if (attribute == "low") {
        value = typep->kind == VType::ENUM ? 0 : std::min(typep->left, typep->right);
        return true;
    }
    if (attribute == "high") {
        value = typep->kind == VType::ENUM ? static_cast<int64_t>(typep->literals.size()) - 1
                                           : std::max(typep->left, typep->right);
        return true;
    }
    if (expr.args.size() != 1) return false;
    int64_t position = 0;
    if (!evalInt(*expr.args[0], position)) return false;
    if (attribute == "pos") {
        value = position;
        return true;
    }
    if (attribute == "val") {
        // IEEE 1076-2008 16.2.2: 'val outside the type's range is an error
        if (typep->kind == VType::ENUM) {
            const int64_t count = static_cast<int64_t>(typep->literals.size());
            if (position < 0 || position >= count) {
                expr.flp->v3error("VHDL: 'val outside the range of the type.");
                return false;
            }
        }
        value = position;
        return true;
    }
    const bool stepAttr = attribute == "succ" || attribute == "pred" || attribute == "leftof"
                          || attribute == "rightof";
    if (!stepAttr || (typep->kind != VType::ENUM && typep->kind != VType::INT)) return false;
    // Neighbouring value in the type's order; an enumeration ascends, an integer type
    // follows its declared direction (IEEE 1076-2008 16.2.2)
    const bool ascending = typep->kind == VType::ENUM || typep->ascending;
    const int64_t low = typep->kind == VType::ENUM ? 0 : std::min(typep->left, typep->right);
    const int64_t high = typep->kind == VType::ENUM
                             ? static_cast<int64_t>(typep->literals.size()) - 1
                             : std::max(typep->left, typep->right);
    const bool up = attribute == "succ" || (attribute == "rightof" && ascending)
                    || (attribute == "leftof" && !ascending);
    const int64_t next = up ? position + 1 : position - 1;
    const bool isEnum = typep->kind == VType::ENUM;
    if (position < low || position > high) {
        expr.flp->v3error("VHDL: '" + std::string{attribute} + "' value is outside the "
                          + (isEnum ? "enumeration type." : "type."));
        return false;
    }
    if (next < low || next > high) {
        expr.flp->v3error("VHDL: '" + std::string{attribute} + "' of the "
                          + (up ? "last " : "first ") + (isEnum ? "enumeration literal" : "value")
                          + " is outside the type.");
        return false;
    }
    value = next;
    return true;
}

//! Integer value of a binary operation on two constant operands
bool Lowering::evalIntBinary(const Expr& expr, int64_t& value) const {
    // An expression with a real operand is a real expression: dividing two integers
    // here would truncate each step instead of the result
    if (hasRealLiteral(expr)) {
        double realValue = 0.0;
        if (!evalReal(expr, realValue)) return false;
        // The value is consumed as an integer, so it rounds like a conversion
        value = static_cast<int64_t>(std::llround(realValue));
        return true;
    }
    int64_t lhs = 0;
    int64_t rhs = 0;
    if (!evalInt(*expr.a, lhs) || !evalInt(*expr.b, rhs)) return false;
    const std::string& op = expr.text;
    if (op == "+") {
        value = lhs + rhs;
    } else if (op == "-") {
        value = lhs - rhs;
    } else if (op == "*") {
        value = lhs * rhs;
    } else if (op == "/") {
        if (rhs == 0) return false;
        value = lhs / rhs;
    } else if (op == "rem") {
        if (rhs == 0) return false;
        value = lhs - (lhs / rhs) * rhs;
    } else if (op == "mod") {
        if (rhs == 0) return false;
        value = lhs - (lhs / rhs) * rhs;
        if (value != 0 && ((value < 0) != (rhs < 0))) value += rhs;
    } else if (op == "**") {
        // Integer exponentiation with a non-negative exponent
        if (rhs < 0) return false;
        int64_t result = 1;
        int64_t base = lhs;
        int64_t exponent = rhs;
        while (exponent > 0) {
            if (exponent & 1) {
                if (__builtin_mul_overflow(result, base, &result)) return false;
            }
            exponent >>= 1;
            if (exponent && __builtin_mul_overflow(base, base, &base)) return false;
        }
        value = result;
    } else {
        return false;
    }
    return true;
}

//! Integer value of an indexed call, either an inlined pure function or a builtin
bool Lowering::evalIntIndex(const Expr& expr, int64_t& value) const {
    // The callee may be qualified (`ieee.math_real.round(...)`), whose selected name
    // is the function itself
    if (!expr.a || (expr.a->kind != Expr::NAME && expr.a->kind != Expr::SELECTED)) return false;
    const Expr* returnp = nullptr;
    if (pushPureCall(expr, returnp)) {
        const bool folded = evalInt(*returnp, value);
        popPureCall();
        if (folded) return true;
    }
    const std::string& fn = expr.a->text;
    if (expr.args.size() == 2) {
        int64_t lhs = 0;
        int64_t rhs = 0;
        if ((fn != "min" && fn != "max") || !evalInt(*expr.args[0], lhs)
            || !evalInt(*expr.args[1], rhs))
            return false;
        value = fn == "min" ? std::min(lhs, rhs) : std::max(lhs, rhs);
        return true;
    }
    if (expr.args.size() != 1) return false;
    // real(integer) is integral, so it folds as an integer and the real expression
    // around it stays inside the constant folders
    if (fn == "real") return evalInt(*expr.args[0], value);
    double real = 0.0;
    if (!evalReal(*expr.args[0], real)) return false;
    if (fn == "ceil") {
        value = static_cast<int64_t>(std::ceil(real));
    } else if (fn == "floor") {
        value = static_cast<int64_t>(std::floor(real));
    } else if (fn == "trunc") {
        value = static_cast<int64_t>(std::trunc(real));
    } else if (fn == "round" || fn == "natural" || fn == "positive" || fn == "integer") {
        // A conversion to an integer type rounds to nearest, as IEEE 1076-2008 9.3.6
        // says; only trunc() truncates
        value = static_cast<int64_t>(std::llround(real));
    } else {
        return false;
    }
    return true;
}

bool Lowering::staticIntFold(const ExprP& exprp, int64_t& value) {
    if (!exprp) return false;
    if (evalInt(*exprp, value)) return true;
    // A pure function call is static too; inline and fold it
    AstNodeExpr* valuep = lowerExpr(*exprp, nullptr);
    if (!valuep) return false;
    AstConst* const constp = foldStatic(valuep);
    valuep->deleteTree();
    if (!constp) return false;
    value = constp->num().toSInt();
    constp->deleteTree();
    return true;
}

//! Bind `call`'s actuals to the formals of a pure function whose returned expression the
//! folders can evaluate, so that expression becomes a closed term. Only a body that is a
//! single return qualifies: anything else may compute effects the folders cannot model.
//! A call is left unfolded when no single visible declaration fits, rather than guessing
//! between overloads. On success the caller pops with popPureCall().
bool Lowering::pushPureCall(const Expr& call, const Expr*& returnp) const {
    returnp = nullptr;
    // A call is written `name(...)`, except a function without a formal, which may also
    // be written as the bare name
    const std::string* namep = nullptr;
    if (call.kind == Expr::NAME) {
        namep = &call.text;
    } else if (call.a && call.a->kind == Expr::NAME) {
        namep = &call.a->text;
    } else {
        return false;
    }
    const SubprogramDecl* candidatep = nullptr;
    for (const SubprogramDecl* const subp : visibleSubprograms(*namep)) {
        if (subp->kind != SubprogramDecl::FUNCTION || subp->impure || !subp->hasBody) continue;
        if (std::any_of(m_pureCalls.cbegin(), m_pureCalls.cend(),
                        [=](const PureCall& frame) { return frame.subp == subp; })) {
            continue;
        }
        if (subp->body.size() != 1 || !subp->body[0] || subp->body[0]->kind != Stmt::RETURN
            || !subp->body[0]->value)
            continue;
        std::size_t slots = 0;
        for (const FormalDecl& formal : subp->formals) slots += formal.names.size();
        if (slots != call.args.size()) continue;
        bool positional = true;
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            if (!call.args[i]) positional = false;
            if (i < call.formalNames.size() && !call.formalNames[i].empty()) positional = false;
        }
        if (!positional) continue;
        if (!formalsAccept(call, *subp)) continue;
        if (candidatep) return false;
        candidatep = subp;
    }
    if (!candidatep) return false;
    PureCall frame;
    std::size_t slot = 0;
    for (const FormalDecl& formal : candidatep->formals) {
        for (const auto& name : formal.names) {
            // Value the actual here, in the caller's scope: the body is then evaluated
            // over literals only, so no name of the caller can leak into it.
            const Expr& actual = *call.args[slot++];
            auto valuep = std::make_unique<Expr>(Expr::INT_LIT);
            valuep->flp = actual.flp;
            int64_t intValue = 0;
            double realValue = 0.0;
            if (evalInt(actual, intValue)) {
                valuep->intValue = intValue;
            } else if (evalReal(actual, realValue)) {
                valuep->kind = Expr::REAL_LIT;
                valuep->realValue = realValue;
            } else {
                return false;
            }
            frame.args[name.first] = std::move(valuep);
        }
    }
    frame.subp = candidatep;
    m_pureCalls.push_back(std::move(frame));
    // Constants of the body's declarative part are folded on top of the formals, which a
    // helper such as `constant log2_value : natural := floor_log2(value);` needs
    for (const Decl& decl : candidatep->decls) {
        auto valuep = std::make_unique<Expr>(Expr::INT_LIT);
        valuep->flp = decl.flp;
        int64_t intValue = 0;
        double realValue = 0.0;
        if (decl.kind == Decl::CONSTANT && decl.init && evalInt(*decl.init, intValue)) {
            valuep->intValue = intValue;
        } else if (decl.kind == Decl::CONSTANT && decl.init && evalReal(*decl.init, realValue)) {
            valuep->kind = Expr::REAL_LIT;
            valuep->realValue = realValue;
        } else {
            popPureCall();
            return false;
        }
        for (const auto& name : decl.names) {
            auto binding = std::make_unique<Expr>(valuep->kind);
            binding->flp = valuep->flp;
            binding->intValue = valuep->intValue;
            binding->realValue = valuep->realValue;
            m_pureCalls.back().args[name.first] = std::move(binding);
        }
    }
    if (!closedOverFormals(*candidatep->body[0]->value, m_pureCalls.back().args)) {
        popPureCall();
        return false;
    }
    returnp = candidatep->body[0]->value.get();
    return true;
}

void Lowering::popPureCall() const { m_pureCalls.pop_back(); }

bool Lowering::evalReal(const Expr& expr, double& value) const {
    switch (expr.kind) {
    case Expr::INT_LIT: value = static_cast<double>(expr.intValue); return true;
    case Expr::PHYS_LIT: {
        int64_t scaled = 0;
        if (!physicalValue(*m_designp, expr, scaled)) return false;
        value = static_cast<double>(scaled);
        return true;
    }
    case Expr::REAL_LIT: value = expr.realValue; return true;
    case Expr::NAME: {
        if (!m_pureCalls.empty()) {
            const std::map<std::string, ExprP>& args = m_pureCalls.back().args;
            const auto argIt = args.find(expr.text);
            if (argIt != args.end()) return evalReal(*argIt->second, value);
        }
        const auto realIt = m_realConsts.find(expr.text);
        if (realIt != m_realConsts.end()) {
            value = realIt->second;
            return true;
        }
        int64_t intValue = 0;
        if (!evalInt(expr, intValue)) return false;
        value = static_cast<double>(intValue);
        return true;
    }
    case Expr::ATTR: {
        // An attribute of a discrete type or name is an integer
        int64_t intValue = 0;
        if (!evalInt(expr, intValue)) return false;
        value = static_cast<double>(intValue);
        return true;
    }
    case Expr::PAREN: return evalReal(*expr.a, value);
    case Expr::UNARY: {
        double operand = 0.0;
        if (!evalReal(*expr.a, operand)) return false;
        if (expr.text == "+") {
            value = operand;
        } else if (expr.text == "-") {
            value = -operand;
        } else if (expr.text == "abs") {
            value = std::fabs(operand);
        } else {
            return false;
        }
        return true;
    }
    case Expr::BINARY: {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!evalReal(*expr.a, lhs) || !evalReal(*expr.b, rhs)) return false;
        const std::string& op = expr.text;
        if (op == "+") {
            value = lhs + rhs;
        } else if (op == "-") {
            value = lhs - rhs;
        } else if (op == "*") {
            value = lhs * rhs;
        } else if (op == "/") {
            if (rhs == 0.0) return false;
            value = lhs / rhs;
        } else if (op == "**") {
            value = std::pow(lhs, rhs);
        } else {
            return false;
        }
        return true;
    }
    case Expr::INDEX: {
        // The callee may be qualified (`ieee.math_real.round(...)`), whose selected name
        // is the function itself
        if (!expr.a || (expr.a->kind != Expr::NAME && expr.a->kind != Expr::SELECTED))
            return false;
        const Expr* returnp = nullptr;
        if (pushPureCall(expr, returnp)) {
            const bool folded = evalReal(*returnp, value);
            popPureCall();
            if (folded) return true;
        }
        const std::string& fn = expr.a->text;
        if (expr.args.size() == 2) {
            double lhs = 0.0;
            double rhs = 0.0;
            if ((fn != "min" && fn != "max") || !evalReal(*expr.args[0], lhs)
                || !evalReal(*expr.args[1], rhs))
                return false;
            value = fn == "min" ? std::min(lhs, rhs) : std::max(lhs, rhs);
            return true;
        }
        if (expr.args.size() != 1) return false;
        double operand = 0.0;
        if (!evalReal(*expr.args[0], operand)) return false;
        if (fn == "real") {
            value = operand;
        } else if (fn == "ceil") {
            value = std::ceil(operand);
        } else if (fn == "floor") {
            value = std::floor(operand);
        } else if (fn == "round") {
            value = std::round(operand);
        } else if (fn == "trunc") {
            value = std::trunc(operand);
        } else if (fn == "log2") {
            if (operand <= 0.0) return false;
            value = std::log2(operand);
        } else if (fn == "log") {
            if (operand <= 0.0) return false;
            value = std::log(operand);
        } else if (fn == "exp") {
            value = std::exp(operand);
        } else if (fn == "sqrt") {
            if (operand < 0.0) return false;
            value = std::sqrt(operand);
        } else if (fn == "sin") {
            value = std::sin(operand);
        } else if (fn == "cos") {
            value = std::cos(operand);
        } else if (fn == "tan") {
            value = std::tan(operand);
        } else if (fn == "asin") {
            if (operand < -1.0 || operand > 1.0) return false;
            value = std::asin(operand);
        } else if (fn == "acos") {
            if (operand < -1.0 || operand > 1.0) return false;
            value = std::acos(operand);
        } else if (fn == "atan") {
            value = std::atan(operand);
        } else if (fn == "log10") {
            if (operand <= 0.0) return false;
            value = std::log10(operand);
        } else if (fn == "integer" || fn == "natural" || fn == "positive") {
            value = static_cast<double>(static_cast<int64_t>(std::llround(operand)));
        } else {
            return false;
        }
        return true;
    }
    default: return false;
    }
}

//! Text of a string literal or of a string generic visible here
bool Lowering::stringOperandText(const Expr& expr, std::string& out) const {
    switch (expr.kind) {
    case Expr::STRING_LIT: out = expr.spelling; return true;
    case Expr::BITS_LIT: out = expr.bits; return true;
    case Expr::PAREN: return expr.a && stringOperandText(*expr.a, out);
    case Expr::NAME: {
        const auto it = m_stringConsts.find(expr.text);
        if (it == m_stringConsts.end()) return false;
        out = it->second;
        return true;
    }
    default: return false;
    }
}

bool Lowering::evalCond(const Expr& expr, bool& value) const {
    switch (expr.kind) {
    case Expr::PAREN: return evalCond(*expr.a, value);
    case Expr::TRUE_LIT: value = true; return true;
    case Expr::FALSE_LIT: value = false; return true;
    case Expr::NAME: {
        if (expr.text == "true" || expr.text == "false") {
            value = expr.text == "true";
            return true;
        }
        // A function without a formal may be written as the bare name
        const Expr* returnp = nullptr;
        if (!pushPureCall(expr, returnp)) return false;
        const bool folded = evalCond(*returnp, value);
        popPureCall();
        return folded;
    }
    case Expr::INDEX: {
        if (!expr.a || expr.a->kind != Expr::NAME) return false;
        const Expr* returnp = nullptr;
        if (!pushPureCall(expr, returnp)) return false;
        const bool folded = evalCond(*returnp, value);
        popPureCall();
        return folded;
    }
    case Expr::UNARY: {
        bool operand = false;
        if (expr.text != "not" || !evalCond(*expr.a, operand)) return false;
        value = !operand;
        return true;
    }
    case Expr::BINARY: {
        const std::string& op = expr.text;
        if (op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor") {
            bool lhs = false;
            bool rhs = false;
            if (!evalCond(*expr.a, lhs) || !evalCond(*expr.b, rhs)) return false;
            if (op == "and")
                value = lhs && rhs;
            else if (op == "or")
                value = lhs || rhs;
            else if (op == "xor")
                value = lhs != rhs;
            else if (op == "nand")
                value = !(lhs && rhs);
            else
                value = !(lhs || rhs);
            return true;
        }
        if (op != "=" && op != "/=" && op != "<" && op != "<=" && op != ">" && op != ">=") {
            return false;
        }
        if (op == "=" || op == "/=") {
            // A string generic's text is fixed at elaboration, so comparing it with a
            // literal folds even when it is empty and therefore has no packed value
            std::string lhsText;
            std::string rhsText;
            if (stringOperandText(*expr.a, lhsText) && stringOperandText(*expr.b, rhsText)) {
                value = (lhsText == rhsText) == (op == "=");
                return true;
            }
        }
        // A real never compares with an integer; the lowered operator reports it
        VType lhsType;
        VType rhsType;
        if (actualType(*expr.a, lhsType) && actualType(*expr.b, rhsType)
            && (lhsType.kind == VType::REAL) != (rhsType.kind == VType::REAL)) {
            return false;
        }
        double lhs = 0.0;
        double rhs = 0.0;
        int64_t lhsInt = 0;
        int64_t rhsInt = 0;
        if (evalInt(*expr.a, lhsInt) && evalInt(*expr.b, rhsInt)) {
            lhs = static_cast<double>(lhsInt);
            rhs = static_cast<double>(rhsInt);
        } else if (!evalReal(*expr.a, lhs) || !evalReal(*expr.b, rhs)) {
            return false;
        }
        if (op == "=")
            value = lhs == rhs;
        else if (op == "/=")
            value = lhs != rhs;
        else if (op == "<")
            value = lhs < rhs;
        else if (op == "<=")
            value = lhs <= rhs;
        else if (op == ">")
            value = lhs > rhs;
        else
            value = lhs >= rhs;
        return true;
    }
    default: return false;
    }
}

//! True when the expression tree has a real operand: a real literal or a name bound to
//! an elaboration-time real constant. Gates the real comparison fold so integer-only
//! comparisons are never routed through it.
bool Lowering::mentionsReal(const Expr& expr) const {
    switch (expr.kind) {
    case Expr::REAL_LIT: return true;
    case Expr::NAME: return m_realConsts.count(expr.text) != 0;
    case Expr::PAREN: return expr.a && mentionsReal(*expr.a);
    case Expr::UNARY: return expr.a && mentionsReal(*expr.a);
    case Expr::BINARY:
        return (expr.a && mentionsReal(*expr.a)) || (expr.b && mentionsReal(*expr.b));
    case Expr::INDEX:
        for (const ExprP& argp : expr.args)
            if (argp && mentionsReal(*argp)) return true;
        return false;
    default: return false;
    }
}

//! A real survives only as an elaboration-time constant; reject any object that would
//! need a runtime floating type so it reports as unsupported instead of emitting a
//! variable with no data type. Likewise reject an object whose type stayed unconstrained
//! (null data type); ports defer their width to a binding, so they opt out.
bool Lowering::rejectUnmodelledObject(const VType& type, FileLine* flp, bool allowUnconstrained) {
    if (type.kind == VType::REAL) {
        unsupportedError(flp, "real variable, signal, or port");
        return true;
    }
    // A physical value is a count of its step unit only while elaborating: the subset
    // has no run-time physical object (5.14)
    if (type.kind == VType::INT && type.kwd == VBasicDTypeKwd::LONGINT) {
        unsupportedError(flp, "physical value at run time");
        return true;
    }
    if (!allowUnconstrained && !type.dtp) {
        unsupportedError(flp, "object of an unconstrained array type");
        return true;
    }
    // An array or record whose element or member is real or physical has the same
    // missing run-time model, but the checks above see only the object's own type.
    // Walk the element and member types so a later conversion or comparison cannot
    // dereference the element's null dtype.
    static constexpr int MAX_NESTING = 32;
    const auto rejectNested = [&](const VType& nested, const auto& self, int depth) -> bool {
        if (depth > MAX_NESTING) v3fatalSrc("VHDL type nesting exceeds the supported depth");
        if (nested.kind == VType::REAL) {
            unsupportedError(flp, "real object element");
            return true;
        }
        if (nested.kind == VType::INT && nested.kwd == VBasicDTypeKwd::LONGINT) {
            unsupportedError(flp, "physical object at run time");
            return true;
        }
        if (nested.isVector() && nested.elemp) return self(*nested.elemp, self, depth + 1);
        if (nested.kind == VType::RECORD) {
            for (const VField& field : nested.fields) {
                if (field.type && self(*field.type, self, depth + 1)) return true;
            }
        }
        return false;
    };
    return rejectNested(type, rejectNested, 0);
}

AstNodeDType* Lowering::enumDType(FileLine* flp, const TypeDecl& decl, int width) {
    AstBasicDType* const basep = m_rootp->typeTablep()->findLogicBitDType(
        flp, VBasicDTypeKwd::LOGIC, width, width, VSigning::UNSIGNED);
    AstEnumItem* itemsp = nullptr;
    for (size_t i = 0; i < decl.literals.size(); ++i) {
        AstEnumItem* const itemp
            = new AstEnumItem{flp, decl.literals[i].second, nullptr,
                              new AstConst{flp, V3Number{flp, width, static_cast<uint32_t>(i)}}};
        itemsp = itemsp ? AstNode::addNext<AstEnumItem, AstEnumItem>(itemsp, itemp) : itemp;
    }
    // The constructor takes the basis type as a child (op1), which is only legal for
    // parser dtypes not yet in the type table. Give it a throw-away basis, then
    // reference the shared basic dtype and drop the child to reach the post-width shape.
    AstBasicDType* const basisp
        = new AstBasicDType{flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, width, width};
    AstEnumDType* const enump = new AstEnumDType{flp, VFlagChildDType{}, basisp, itemsp};
    enump->refDTypep(basep);
    basisp->unlinkFrBack();
    VL_DO_DANGLING(basisp->deleteTree(), basisp);
    enump->dtypep(enump);
    for (AstEnumItem* const itemp : enump->itemspList()) itemp->dtypep(enump);
    m_rootp->typeTablep()->addTypesp(enump);
    return enump;
}

void Lowering::registerType(const TypeDecl& decl) {
    FileLine* const flp = decl.flp;
    VType type;
    switch (decl.kind) {
    case TypeDecl::SUBTYPE:
        if (!resolveType(decl.base, type)) return;
        break;
    case TypeDecl::PHYSICAL:
        // Like the predefined `time', a physical value is a count of the step unit, so the
        // type is a plain 64-bit integer and only the literals carry the units
        type = timeType(flp);
        addPhysicalUnits(decl, *m_designp);
        break;
    case TypeDecl::ENUM: registerEnumType(flp, decl, type); break;
    case TypeDecl::ARRAY:
        if (!registerArrayType(decl, type)) return;
        break;
    case TypeDecl::RECORD:
        if (!registerRecordType(decl, type)) return;
        break;
    }
    if (decl.kind == TypeDecl::PHYSICAL) {
        type.typeName = m_unitName.empty() ? decl.name : m_unitName + "." + decl.name;
    } else if (decl.kind != TypeDecl::SUBTYPE && type.typeName.empty()) {
        type.typeName = m_unitName.empty() ? decl.name : m_unitName + "." + decl.name;
    }
    m_types[decl.name] = type;
    m_ambiguousTypes.erase(decl.name);
}

//! Register an enumeration's dtype and its literal positions
void Lowering::registerEnumType(FileLine* flp, const TypeDecl& decl, VType& type) {
    int width = 1;
    while ((1 << width) < static_cast<int>(decl.literals.size())) ++width;
    type.kind = VType::ENUM;
    type.width = width;
    type.dtp = enumDType(flp, decl, width);
    for (const auto& literal : decl.literals) type.literals.push_back(literal.first);
    for (size_t i = 0; i < decl.literals.size(); ++i) {
        AstConst* const constp = new AstConst{flp, V3Number{flp, width, static_cast<uint32_t>(i)}};
        constp->dtypep(type.dtp);
        const auto existing = m_enumConsts.find(decl.literals[i].first);
        if (existing != m_enumConsts.end() && existing->second->dtypep() != type.dtp) {
            m_ambiguousLiterals.insert(decl.literals[i].first);
        }
        m_enumConsts[decl.literals[i].first] = constp;
    }
}

//! Element type of an array declaration, including a vector or integer_vector base
bool Lowering::resolveArrayElement(const TypeDecl& decl, VType& elem) {
    FileLine* const flp = decl.flp;
    const std::string& baseMark = decl.base.mark;
    const bool unconstrainedVectorBase
        = (baseMark == "std_logic_vector" || baseMark == "std_ulogic_vector"
           || baseMark == "bit_vector" || baseMark == "unsigned" || baseMark == "signed"
           || baseMark == "u_unsigned" || baseMark == "u_signed")
          && !decl.base.hasRange && decl.base.constraintMark.empty();
    if (unconstrainedVectorBase) {
        // An array of an unconstrained vector; a use supplies the element's range
        elem.kind = VType::VECTOR;
        elem.kwd = baseMark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
        elem.signing = vectorSigning(baseMark);
        elem.width = 0;
        elem.elemWidth = 1;
    } else if (baseMark == "integer_vector") {
        // An IEEE unconstrained array of integer: the constraint gives the element's bounds
        int64_t left = 0;
        int64_t right = 0;
        bool ascending = false;
        if (!decl.base.hasRange || !constraintBounds(decl.base, left, right, ascending)) {
            unsupportedError(flp, "non-static array range");
            return false;
        }
        const VType intElem = intType(flp);
        elem.kind = VType::ARRAY;
        elem.kwd = VBasicDTypeKwd::LOGIC;
        elem.signing = VSigning::UNSIGNED;
        elem.elemp = std::make_shared<VType>(intElem);
        elem.elemWidth = intElem.width;
        elem.left = static_cast<int>(left);
        elem.right = static_cast<int>(right);
        elem.ascending = ascending;
        elem.width
            = intElem.width * (std::abs(static_cast<int>(left) - static_cast<int>(right)) + 1);
        elem.dtp = findVectorWidth(flp, elem.kwd, elem.signing, elem.width);
    } else if (!resolveType(decl.base, elem)) {
        return false;
    }
    return true;
}

//! Register an array as a nested array of arrays, one level per index constraint
bool Lowering::registerArrayType(const TypeDecl& decl, VType& type) {
    FileLine* const flp = decl.flp;
    VType elem;
    if (!resolveArrayElement(decl, elem)) return false;
    if (!decl.index.hasRange) {
        // An index that is a type mark takes the mark's range: an enumeration's
        // positions (IEEE 1076-2008 5.2.2.1), or a discrete subtype's own bounds
        const auto indexIt = m_types.find(decl.index.mark);
        const bool enumIndex = indexIt != m_types.end() && indexIt->second.kind == VType::ENUM
                               && !indexIt->second.literals.empty();
        const bool intIndex = indexIt != m_types.end() && indexIt->second.kind == VType::INT;
        if (!enumIndex && !intIndex) {
            unsupportedError(flp, "array type without an index constraint");
            return false;
        }
    }
    type.kind = VType::ARRAY;
    type.signing = VSigning::NOSIGN;
    // Fold the index constraints (outermost first) into a nested array of arrays, so a
    // multi-dimensional array is a packed vector whose element is itself the inner array
    std::vector<const TypeSpec*> specs;
    specs.push_back(&decl.index);
    for (const TypeSpec& extra : decl.indexes) specs.push_back(&extra);
    std::vector<int64_t> lefts(specs.size());
    std::vector<int64_t> rights(specs.size());
    std::vector<char> ascends(specs.size());
    // The bounds of the index subtype itself, per level: an unconstrained array's
    // constant takes one element per aggregate element from the subtype's low bound
    std::vector<int64_t> indexLows(specs.size(), 0);
    for (size_t k = 0; k < specs.size(); ++k) {
        VType indexType;
        const auto markIt = m_types.find(specs[k]->mark);
        if (markIt != m_types.end() && markIt->second.kind == VType::INT) {
            indexLows[k] = markIt->second.intLow;
        } else if (markIt != m_types.end() && markIt->second.kind == VType::ENUM
                   && !markIt->second.literals.empty()) {
            indexLows[k] = 0;
        } else if (predefinedIntegerType(flp, specs[k]->mark, indexType)) {
            indexLows[k] = indexType.left;
        }
    }
    bool constrained = true;
    for (size_t k = 0; k < specs.size(); ++k) {
        if (!specs[k]->hasRange) {
            const auto markIt = m_types.find(specs[k]->mark);
            if (markIt != m_types.end() && markIt->second.kind == VType::INT) {
                // A discrete subtype as the index constraint: its own bounds
                lefts[k] = markIt->second.intLow;
                rights[k] = markIt->second.intHigh;
                ascends[k] = lefts[k] <= rights[k];
                continue;
            }
            // An enumeration index runs over the positions of its literals
            lefts[k] = 0;
            rights[k]
                = static_cast<int64_t>(m_types.find(specs[k]->mark)->second.literals.size()) - 1;
            ascends[k] = true;
            continue;
        }
        bool asc = false;
        if (!constraintBounds(*specs[k], lefts[k], rights[k], asc)) {
            constrained = false;  // Unconstrained: every use supplies the index constraint
            break;
        }
        ascends[k] = asc;
    }
    if (!constrained) {
        // Every level survives, so a use supplies one index constraint per level.
        // The index subtype's low bound is kept: an array constant that states no
        // constraint takes one element per aggregate element from it
        VType inner = elem;
        for (size_t k = specs.size(); k-- > 0;) {
            VType arr;
            arr.kind = VType::ARRAY;
            arr.elemp = std::make_shared<VType>(inner);
            arr.elemWidth = inner.width;
            arr.kwd = inner.kwd;
            arr.signing = VSigning::NOSIGN;
            arr.width = 0;
            arr.intLow = static_cast<int>(indexLows[k]);
            inner = arr;
        }
        inner.unconstrainedBase = true;
        type = inner;
        return true;
    }
    VType cur = elem;
    for (size_t k = specs.size(); k-- > 0;) {
        VType arr;
        arr.kind = VType::ARRAY;
        arr.elemp = std::make_shared<VType>(cur);
        arr.elemWidth = cur.width;
        arr.kwd = cur.kwd;
        arr.signing = VSigning::NOSIGN;
        arr.left = static_cast<int>(lefts[k]);
        arr.right = static_cast<int>(rights[k]);
        arr.ascending = ascends[k];
        arr.width = cur.width * arr.length();
        arr.dtp = findVectorWidth(flp, arr.kwd, VSigning::NOSIGN, arr.width);
        cur = arr;
    }
    type = cur;  // The outermost array type
    return true;
}

//! Register a record as MSB-first fields in a packed vector
bool Lowering::registerRecordType(const TypeDecl& decl, VType& type) {
    FileLine* const flp = decl.flp;
    int total = 0;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::BIT;
    std::vector<VField> fields;
    for (const TypeDecl::Member& member : decl.members) {
        VType memberType;
        const std::string& mark = member.type.mark;
        const bool unconstrainedMember
            = (mark == "std_logic_vector" || mark == "std_ulogic_vector" || mark == "bit_vector"
               || mark == "unsigned" || mark == "signed" || mark == "u_unsigned"
               || mark == "u_signed")
              && !member.type.hasRange && member.type.constraintMark.empty();
        if (unconstrainedMember) {
            // An unconstrained element: its width comes from a subtype or port binding
            memberType.kind = VType::VECTOR;
            memberType.kwd = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
            memberType.signing = vectorSigning(mark);
            memberType.width = 0;
        } else if (!resolveType(member.type, memberType)) {
            return false;
        }
        if (memberType.kwd == VBasicDTypeKwd::LOGIC) kwd = VBasicDTypeKwd::LOGIC;
        // `a, b : t` declares one record element per name, in order
        for (const auto& name : member.names) {
            VField field;
            field.name = name.first;
            field.width = memberType.width;
            field.type = std::make_shared<VType>(memberType);
            fields.push_back(std::move(field));
            total += memberType.width;
        }
    }
    // Declaration order is most significant first
    int offset = total;
    for (VField& field : fields) {
        offset -= field.width;
        field.offset = offset;
    }
    type.kind = VType::RECORD;
    type.width = total;
    type.kwd = kwd;
    type.signing = VSigning::NOSIGN;
    type.fields = std::move(fields);
    // An unconstrained record (no fixed width) has no packed dtype until constrained
    type.dtp = total > 0 ? findVectorWidth(flp, kwd, VSigning::NOSIGN, total)
                         : findBasic(flp, kwd == VBasicDTypeKwd::BIT ? VBasicDTypeKwd::BIT
                                                                     : VBasicDTypeKwd::LOGIC);
    return true;
}

//! Whether an aggregate fills the rest of its elements with an `others` association
bool aggregateHasOthers(const Expr& expr) {
    for (const std::vector<ExprP>& choices : expr.choices) {
        for (const ExprP& choicep : choices) {
            if (choicep && choicep->kind == Expr::NAME && choicep->text == "others") return true;
        }
    }
    return false;
}

//! Whether `expr` names any of `names` anywhere in it
bool Lowering::exprNamesAny(const Expr& expr, const std::set<std::string>& names) {
    if (expr.kind == Expr::NAME && names.count(expr.text)) return true;
    if (expr.a && exprNamesAny(*expr.a, names)) return true;
    if (expr.b && exprNamesAny(*expr.b, names)) return true;
    for (const ExprP& argp : expr.args) {
        if (argp && exprNamesAny(*argp, names)) return true;
    }
    return false;
}

//! Whether one of the type's own or members' constraints names any of `names`
bool Lowering::typeDeclNamesAny(const TypeDecl& decl, const std::set<std::string>& names) {
    const auto specNames = [&names](const TypeSpec& spec) {
        if (spec.left && exprNamesAny(*spec.left, names)) return true;
        if (spec.right && exprNamesAny(*spec.right, names)) return true;
        for (const TypeSpec::MemberConstraint& member : spec.memberConstraints) {
            if (member.lo && exprNamesAny(*member.lo, names)) return true;
            if (member.hi && exprNamesAny(*member.hi, names)) return true;
        }
        return false;
    };
    std::vector<const TypeSpec*> specs;
    specs.push_back(&decl.base);
    specs.push_back(&decl.index);
    for (const TypeSpec& extra : decl.indexes) specs.push_back(&extra);
    for (const TypeDecl::Member& member : decl.members) specs.push_back(&member.type);
    for (const TypeSpec* const specp : specs) {
        if (specNames(*specp)) return true;
        for (const TypeSpec& nested : specp->indexConstraints) {
            if (specNames(nested)) return true;
        }
    }
    return false;
}

//! Whether one of the type's own or members' marks names any of `names`
bool Lowering::typeDeclMarksAny(const TypeDecl& decl, const std::set<std::string>& names) {
    const auto specMarks = [&names](const TypeSpec& spec) {
        if (names.count(spec.mark) || names.count(spec.constraintMark)) return true;
        for (const TypeSpec& nested : spec.indexConstraints) {
            if (names.count(nested.mark) || names.count(nested.constraintMark)) return true;
        }
        return false;
    };
    std::vector<const TypeSpec*> specs;
    specs.push_back(&decl.base);
    specs.push_back(&decl.index);
    for (const TypeSpec& extra : decl.indexes) specs.push_back(&extra);
    for (const TypeDecl::Member& member : decl.members) specs.push_back(&member.type);
    for (const TypeSpec* const specp : specs) {
        if (specMarks(*specp)) return true;
    }
    return false;
}

//! Whether an expression reads the range of a name that is not in scope yet. A type in a
//! declarative part is registered before that part's objects exist, so
//! `array (...) of std_logic_vector(sig'range)` has to wait for `sig`
bool Lowering::exprNeedsLaterObject(const Expr& expr) const {
    if (expr.kind == Expr::ATTR && (expr.text == "range" || expr.text == "reverse_range") && expr.a
        && expr.a->kind == Expr::NAME) {
        const std::string& name = expr.a->text;
        const bool known = activeEnv().count(name) || m_scope.count(name) || m_consts.count(name)
                           || m_preConsts.count(name) || m_constValues.count(name)
                           || m_constTypes.count(name) || m_enumConsts.count(name)
                           || m_types.count(name);
        if (!known) return true;
    }
    if (expr.a && exprNeedsLaterObject(*expr.a)) return true;
    if (expr.b && exprNeedsLaterObject(*expr.b)) return true;
    if (expr.c && exprNeedsLaterObject(*expr.c)) return true;
    for (const ExprP& argp : expr.args) {
        if (argp && exprNeedsLaterObject(*argp)) return true;
    }
    return false;
}

bool Lowering::typeDeclNeedsLaterObject(const TypeDecl& decl) const {
    const auto specNeeds = [this](const TypeSpec& spec) {
        // `object'subtype` and `object'element` read an object declared after the types
        if (!spec.objectName.empty() && !m_scope.count(spec.objectName)
            && !m_types.count(spec.objectName)) {
            return true;
        }
        if (spec.left && exprNeedsLaterObject(*spec.left)) return true;
        if (spec.right && exprNeedsLaterObject(*spec.right)) return true;
        for (const TypeSpec::MemberConstraint& member : spec.memberConstraints) {
            if (member.lo && exprNeedsLaterObject(*member.lo)) return true;
            if (member.hi && exprNeedsLaterObject(*member.hi)) return true;
        }
        return false;
    };
    std::vector<const TypeSpec*> specs;
    specs.push_back(&decl.base);
    specs.push_back(&decl.index);
    for (const TypeSpec& extra : decl.indexes) specs.push_back(&extra);
    for (const TypeDecl::Member& member : decl.members) specs.push_back(&member.type);
    for (const TypeSpec* const specp : specs) {
        if (specNeeds(*specp)) return true;
        for (const TypeSpec& nested : specp->indexConstraints) {
            if (specNeeds(nested)) return true;
        }
    }
    return false;
}

void Lowering::registerTypes(const std::vector<TypeDecl>& types, const std::vector<Decl>& decls) {
    ++m_deferSeq;  // A part inlined while this one is emitted defers types of its own
    std::set<std::string> deferredNames;
    for (const TypeDecl& typeDecl : types) {
        // The pre-scan published the constants of the part it could fold; a constraint
        // naming one of the rest has to wait for the value the emitter computes for it
        std::set<std::string> unpublished;
        for (const Decl& decl : decls) {
            if (decl.kind != Decl::CONSTANT || decl.flp->lineno() > typeDecl.flp->lineno())
                continue;
            for (const auto& name : decl.names) {
                if (m_consts.count(name.first) || m_preConsts.count(name.first)
                    || m_constValues.count(name.first))
                    continue;
                unpublished.insert(name.first);
            }
        }
        // A type naming a type this part already deferred cannot be resolved either: the
        // deferred one is registered only once the part's constants have values
        if ((!unpublished.empty() && typeDeclNamesAny(typeDecl, unpublished))
            || (!deferredNames.empty() && typeDeclMarksAny(typeDecl, deferredNames))
            || typeDeclNeedsLaterObject(typeDecl)) {
            m_deferredTypes[typeDecl.name] = DeferredType{&typeDecl, m_deferSeq};
            deferredNames.insert(typeDecl.name);
            continue;
        }
        registerType(typeDecl);
    }
}

void Lowering::finishDeferredTypes() {
    for (auto it = m_deferredTypes.begin(); it != m_deferredTypes.end();) {
        if (it->second.seq != m_deferSeq) {
            ++it;
            continue;
        }
        const TypeDecl* const declp = it->second.declp;
        it = m_deferredTypes.erase(it);
        registerType(*declp);
    }
    --m_deferSeq;
}

//! Bounds of an index constraint; a discrete range attribute such as `sig'range`
//! contributes the bounds and direction of its prefix
bool Lowering::constraintBounds(const TypeSpec& spec, int64_t& left, int64_t& right,
                                bool& ascending) {
    if (spec.right) {
        if (!staticIntFold(spec.left, left) || !staticIntFold(spec.right, right)) return false;
        ascending = spec.ascending;
        return true;
    }
    if (!spec.left || spec.left->kind != Expr::ATTR
        || (spec.left->text != "range" && spec.left->text != "reverse_range") || !spec.left->a) {
        return false;
    }
    VType prefix;
    if (!typeOfDesignator(*spec.left->a, prefix)) {
        // A range attribute's prefix may be a type mark rather than an object, which only a
        // plain name can name
        const auto typeIt
            = spec.left->a->kind == Expr::NAME ? m_types.find(spec.left->a->text) : m_types.end();
        if (typeIt == m_types.end()) return false;
        prefix = typeIt->second;
    }
    const VType* const levelp = indexRangeLevel(*spec.left, prefix);
    if (!levelp) return false;
    left = levelp->left;
    right = levelp->right;
    ascending = levelp->ascending;
    if (spec.left->text == "reverse_range") {
        // The same bounds in the opposite direction
        left = levelp->right;
        right = levelp->left;
        ascending = !levelp->ascending;
    }
    return true;
}

//! Deep-copy the array element chain so a use-site constraint does not mutate the shared type
VType cloneArrayChain(const VType& type) {
    VType copy = type;
    if (type.kind == VType::ARRAY && type.elemp) {
        copy.elemp = std::make_shared<VType>(cloneArrayChain(*type.elemp));
    }
    return copy;
}

bool Lowering::applyIndexConstraints(const TypeSpec& spec, VType& out) {
    FileLine* const flp = spec.flp;
    out = cloneArrayChain(out);  // The instance owns its levels
    // Descend one array level per index constraint; the innermost element may be a vector
    std::vector<const TypeSpec*> constraints;
    std::vector<VType*> levels;
    VType* levelp = &out;
    constraints.push_back(&spec);
    levels.push_back(levelp);
    for (const TypeSpec& extra : spec.indexConstraints) {
        // A record constraint applies to the element this level holds, like a range does
        if (levelp->kind != VType::ARRAY || !levelp->elemp) {
            unsupportedError(flp, "too many index constraints for the array type");
            return false;
        }
        levelp = levelp->elemp.get();
        constraints.push_back(&extra);
        levels.push_back(levelp);
    }
    // Apply the innermost constraint first, so each outer level sees its element's width
    for (size_t k = constraints.size(); k-- > 0;) {
        VType& level = *levels[k];
        if (!constraints[k]->memberConstraints.empty()) {
            // A record constraint on this level: `t(open)(f(r), g(r))`
            if (!applyMemberConstraints(*constraints[k], level)) return false;
            continue;
        }
        if (level.kind == VType::ARRAY && level.elemp) level.elemWidth = level.elemp->width;
        if (!level.isVector()) {
            unsupportedError(flp, "index constraint on a non-array subtype element");
            return false;
        }
        if (!applyRange(*constraints[k], level)) return false;
    }
    return true;
}

bool Lowering::applyRange(const TypeSpec& spec, VType& type) {
    FileLine* const flp = spec.flp;
    if (spec.hasRange) {
        int64_t left = 0;
        int64_t right = 0;
        bool ascending = false;
        if (!constraintBounds(spec, left, right, ascending)) {
            unsupportedError(flp, "non-static range constraint");
            return false;
        }
        type.left = static_cast<int>(left);
        type.right = static_cast<int>(right);
        type.ascending = ascending;
        if (type.kind == VType::INT) {
            type.intLow = static_cast<int>(std::min(left, right));
            type.intHigh = static_cast<int>(std::max(left, right));
            return true;
        }
        if (type.isVector()) {
            type.width = type.elemWidth * type.length();
            type.dtp = type.kind == VType::ARRAY
                           ? findVectorWidth(flp, type.kwd, type.signing, type.width)
                           : findVector(flp, type.kwd, type.signing, type.left, type.right,
                                        type.ascending);
        } else {
            unsupportedError(flp, "range constraint on a type that is not an array");
            return false;
        }
        return true;
    }
    if (!spec.constraintMark.empty()) {
        // `t(open)` leaves the index range to the use site, which supplies the bounds
        if (spec.constraintMark == "open") return true;
        const auto it = m_types.find(spec.constraintMark);
        if (it == m_types.end()) {
            flp->v3error("VHDL: unknown subtype '" + spec.constraintMark + "'.");
            return false;
        }
        // The subtype's range constrains this type whatever the subtype itself is: an
        // integer subtype gives an array's index range, as `unsigned(Addr_c)` uses it
        type.left = it->second.left;
        type.right = it->second.right;
        type.ascending = it->second.ascending;
        if (type.kind == VType::INT) {
            type.intLow = it->second.intLow;
            type.intHigh = it->second.intHigh;
        } else if (type.isVector()) {
            type.width = type.elemWidth * type.length();
            type.dtp = type.kind == VType::ARRAY
                           ? findVectorWidth(flp, type.kwd, type.signing, type.width)
                           : findVector(flp, type.kwd, type.signing, type.left, type.right,
                                        type.ascending);
        }
        return true;
    }
    return true;
}

//! Constrain one record element to a fixed vector range, keeping its element keyword/signing
void Lowering::setRecordField(FileLine* flp, VField& field, int left, int right, bool ascending) {
    // A member whose element is wider than one bit (a string) keeps that width, so the
    // member's packed size is its element count times the element width
    const int elemWidth = field.type && field.type->elemWidth > 0 ? field.type->elemWidth : 1;
    const int elements = std::abs(left - right) + 1;
    VType constrained;
    constrained.kind = elemWidth > 1 ? VType::ARRAY : VType::VECTOR;
    constrained.kwd = VBasicDTypeKwd::LOGIC;
    constrained.signing = VSigning::UNSIGNED;
    if (field.type) {
        constrained.kwd = field.type->kwd;
        constrained.signing = field.type->signing;
        if (elemWidth > 1) constrained.elemp = field.type->elemp;
        constrained.typeName = field.type->typeName;
    }
    constrained.elemWidth = elemWidth;
    constrained.left = left;
    constrained.right = right;
    constrained.ascending = ascending;
    constrained.width = elements * elemWidth;
    constrained.dtp
        = findVectorWidth(flp, constrained.kwd, constrained.signing, constrained.width);
    field.type = std::make_shared<VType>(constrained);
    field.width = constrained.width;
}

//! Recompute a record's total width, MSB-first element offsets and packed dtype
void Lowering::finalizeRecordFields(FileLine* flp, VType& rec) {
    int total = 0;
    for (VField& f : rec.fields) total += f.width;
    int offset = total;
    for (VField& f : rec.fields) {
        offset -= f.width;
        f.offset = offset;
    }
    rec.width = total;
    if (total > 0) rec.dtp = findVectorWidth(flp, rec.kwd, VSigning::UNSIGNED, total);
}

//! A copy of a subtype indication: its bound expressions are owned, so they are cloned
TypeSpec Lowering::copySpec(const TypeSpec& spec) {
    TypeSpec out;
    out.mark = spec.mark;
    out.pkg = spec.pkg;
    out.objectName = spec.objectName;
    out.elementMark = spec.elementMark;
    out.flp = spec.flp;
    out.hasRange = spec.hasRange;
    out.ascending = spec.ascending;
    out.constraintMark = spec.constraintMark;
    if (spec.left) out.left = cloneExpr(*spec.left);
    if (spec.right) out.right = cloneExpr(*spec.right);
    for (const TypeSpec::MemberConstraint& member : spec.memberConstraints) {
        TypeSpec::MemberConstraint copy;
        copy.name = member.name;
        copy.ascending = member.ascending;
        if (member.lo) copy.lo = cloneExpr(*member.lo);
        if (member.hi) copy.hi = cloneExpr(*member.hi);
        if (member.range) copy.range = cloneExpr(*member.range);
        for (const TypeSpec& element : member.elementConstraints) {
            copy.elementConstraints.push_back(copySpec(element));
        }
        out.memberConstraints.push_back(std::move(copy));
    }
    for (const TypeSpec& nested : spec.indexConstraints) {
        out.indexConstraints.push_back(copySpec(nested));
    }
    return out;
}

//! Text of a packed string constant: one 8-bit character per element, first character
//! in the most significant bits, as `makeStringConst` packs them
std::string stringTextOfConst(AstConst* constp) {
    const int width = constp->width();
    if (width <= 0 || width % 8 != 0) return std::string{};
    const int length = width / 8;
    std::string text;
    for (int i = 0; i < length; ++i) {
        unsigned code = 0;
        const int base = (length - 1 - i) * 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (constp->num().bitIs1(base + bit)) code |= 1u << bit;
        }
        text.push_back(static_cast<char>(code));
    }
    return text;
}

//! A record type whose members are unconstrained takes their bounds from the constant's
//! aggregate, as an array takes its bounds from its initial value (IEEE 1076-2008 6.4.2.1)
void Lowering::completeRecordType(TypeSpec& spec, const Expr& init) {
    VType rec;
    if (init.kind != Expr::AGG_NAMED || !resolveType(spec, rec) || rec.kind != VType::RECORD) {
        return;
    }
    for (std::size_t i = 0; i < init.args.size() && i < init.choices.size(); ++i) {
        const std::vector<ExprP>& choices = init.choices[i];
        if (choices.size() != 1 || !choices[0] || choices[0]->kind != Expr::NAME) continue;
        const std::string& fieldName = choices[0]->text;
        const VField* fieldp = nullptr;
        for (const VField& candidate : rec.fields) {
            if (candidate.name == fieldName) fieldp = &candidate;
        }
        if (!fieldp || !fieldp->type || fieldp->type->width != 0) continue;
        const Expr& value = *init.args[i];
        int elements = 0;
        if (value.kind == Expr::BITS_LIT) {
            // A bit-string literal spells one character per element, whether the member's
            // elements are bits or characters
            elements = static_cast<int>(value.bits.size());
        } else if (value.kind == Expr::STRING_LIT) {
            elements = static_cast<int>(value.spelling.size());
        }
        if (elements <= 0) continue;
        TypeSpec::MemberConstraint member;
        member.name = fieldName;
        member.ascending = false;  // A value's range runs downwards, as a literal's does
        member.lo = std::make_unique<Expr>(Expr::INT_LIT);
        member.lo->flp = init.flp;
        member.lo->intValue = elements - 1;
        member.hi = std::make_unique<Expr>(Expr::INT_LIT);
        member.hi->flp = init.flp;
        member.hi->intValue = 0;
        spec.memberConstraints.push_back(std::move(member));
    }
}

bool Lowering::applyMemberConstraints(const TypeSpec& spec, VType& out) {
    FileLine* const flp = spec.flp;
    if (out.kind != VType::RECORD) {
        unsupportedError(flp, "constraint on a type that is not a record");
        return false;
    }
    for (const TypeSpec::MemberConstraint& member : spec.memberConstraints) {
        VField* fieldp = nullptr;
        for (VField& f : out.fields) {
            if (f.name == member.name) fieldp = &f;
        }
        if (!fieldp) {
            flp->v3error("VHDL: record '" + spec.mark + "' has no element '" + member.name + "'.");
            return false;
        }
        if (fieldp->type && fieldp->type->kind == VType::RECORD) {
            unsupportedError(flp, "constraint on a nested record element");
            return false;
        }
        int64_t lo = 0;
        int64_t hi = 0;
        bool ascending = member.ascending;
        if (member.range) {
            // The member takes its range from another object: `rec_t(f(sig'range))`
            TypeSpec range;
            range.flp = member.range->flp;
            range.left = cloneExpr(*member.range);
            range.hasRange = true;
            if (!constraintBounds(range, lo, hi, ascending)) {
                unsupportedError(flp, "non-static record element constraint");
                return false;
            }
        } else if (!staticIntFold(member.lo, lo) || !staticIntFold(member.hi, hi)) {
            unsupportedError(flp, "non-static record element constraint");
            return false;
        }
        if (!member.elementConstraints.empty()) {
            // The elements are constrained first, so the member's width counts them
            TypeSpec chain;
            chain.flp = flp;
            for (const TypeSpec& element : member.elementConstraints) {
                chain.indexConstraints.push_back(copySpec(element));
            }
            VType memberType = *fieldp->type;
            if (!applyIndexConstraints(chain, memberType)) return false;
            fieldp->type = std::make_shared<VType>(memberType);
        }
        setRecordField(flp, *fieldp, static_cast<int>(lo), static_cast<int>(hi), ascending);
    }
    finalizeRecordFields(flp, out);
    return true;
}

bool Lowering::resolveType(const TypeSpec& spec, VType& out) {
    FileLine* const flp = spec.flp;
    if (!flp) return false;
    if (!spec.objectName.empty()) {  // `name'subtype` or `name'element`
        // `name'element` names the element of an array, which the name may denote either
        // as a type or as an object
        if (spec.elementMark) {
            VType base;
            const auto typeIt = m_types.find(spec.objectName);
            if (typeIt != m_types.end()) {
                base = typeIt->second;
            } else {
                Expr prefix{Expr::NAME};
                prefix.flp = flp;
                prefix.text = spec.objectName;
                prefix.spelling = spec.objectName;
                if (!typeOfDesignator(prefix, base)) return false;
            }
            if (!base.isVector() || !base.elemp) {
                unsupportedError(flp, "'element of a type that is not an array");
                return false;
            }
            out = *base.elemp;
            return true;
        }
        Expr prefix{Expr::NAME};
        prefix.flp = flp;
        prefix.text = spec.objectName;
        prefix.spelling = spec.objectName;
        return typeOfDesignator(prefix, out);
    }
    const std::string& mark = spec.mark;
    if (!spec.pkg.empty() && m_packagesp) {
        // A qualified mark names one package's type even when another imported package
        // declares the same name
        const auto pkgIt = m_packagesp->packages.find(spec.pkg);
        if (pkgIt != m_packagesp->packages.end()) {
            const auto typeIt = pkgIt->second.types.find(mark);
            if (typeIt != pkgIt->second.types.end()) {
                out = typeIt->second;
                if (out.typeName.empty()) out.typeName = spec.pkg + "." + mark;
                if (!spec.memberConstraints.empty() && !applyMemberConstraints(spec, out)) {
                    return false;
                }
                if (!spec.indexConstraints.empty()) return applyIndexConstraints(spec, out);
                return applyRange(spec, out);
            }
        }
    }
    if (m_ambiguousTypes.count(mark)) {
        flp->v3error("VHDL: type name '" + mark + "' is ambiguous between imported packages.");
        return false;
    }
    auto declared = m_types.find(mark);
    if (declared == m_types.end() && m_inlineFrames.empty()) {
        // The part deferred this type because its constraint named a constant with no value
        // yet; at a use outside an inlining the part's items are all visible
        const auto deferred = m_deferredTypes.find(mark);
        if (deferred != m_deferredTypes.end() && deferred->second.seq == m_deferSeq) {
            const TypeDecl* const declp = deferred->second.declp;
            m_deferredTypes.erase(deferred);
            registerType(*declp);
            declared = m_types.find(mark);
        }
    }
    if (declared == m_types.end() && !spec.pkg.empty()) {
        // A qualified reference such as `work.defs.rec` may name a package the unit
        // never imported with a `use` clause; import it so the mark resolves
        importPackage(spec.pkg);
        declared = m_types.find(mark);
    }
    if (declared != m_types.end()) {
        out = declared->second;
        if (out.typeName.empty()) out.typeName = mark;
        if (!spec.memberConstraints.empty() && !applyMemberConstraints(spec, out)) return false;
        if (!spec.indexConstraints.empty()) return applyIndexConstraints(spec, out);
        return applyRange(spec, out);
    }
    return resolveTypePredefined(spec, mark, out);
}

bool Lowering::checkStdTypeVisible(const TypeSpec& spec, const std::string& mark) {
    // The packages that declare each type of the IEEE libraries; the types of package
    // STANDARD are always visible
    static const std::map<std::string, std::vector<std::string>> declaredBy
        = {{"std_logic", {"std_logic_1164"}},
           {"std_ulogic", {"std_logic_1164"}},
           {"std_logic_vector", {"std_logic_1164"}},
           {"std_ulogic_vector", {"std_logic_1164"}},
           {"unsigned", {"numeric_std", "numeric_bit", "std_logic_arith"}},
           {"signed", {"numeric_std", "numeric_bit", "std_logic_arith"}},
           {"u_unsigned", {"numeric_std", "numeric_bit", "std_logic_arith"}},
           {"u_signed", {"numeric_std", "numeric_bit", "std_logic_arith"}},
           {"sfixed", {"fixed_pkg", "fixed_generic_pkg"}},
           {"ufixed", {"fixed_pkg", "fixed_generic_pkg"}},
           {"u_sfixed", {"fixed_pkg", "fixed_generic_pkg"}},
           {"u_ufixed", {"fixed_pkg", "fixed_generic_pkg"}}};
    const auto declared = declaredBy.find(mark);
    if (declared == declaredBy.end()) return true;
    for (const std::string& pkg : declared->second) {
        if (spec.pkg == pkg || m_stdUses.count(pkg) || m_stdUses.count(pkg + "." + mark)) {
            return true;
        }
    }
    spec.flp->v3error("VHDL: type '" + mark + "' is not visible; a use clause for package '"
                      + declared->second.front() + "' is required in this design unit"
                      + " (IEEE 1076-2008 12.4).");
    return false;
}

bool Lowering::resolveTypePredefined(const TypeSpec& spec, const std::string& mark, VType& out) {
    FileLine* const flp = spec.flp;
    if (!checkStdTypeVisible(spec, mark)) return false;
    if (mark == "u_unsigned" || mark == "u_signed") {
        // An unconstrained numeric_std vector type; the use site supplies the index range
        if (!spec.hasRange && spec.constraintMark.empty()) {
            unsupportedError(flp, "unconstrained array type mark without an index constraint");
            return false;
        }
        out = vectorType(flp, VBasicDTypeKwd::LOGIC,
                         mark == "u_signed" ? VSigning::SIGNED : VSigning::UNSIGNED, 1);
        out.typeName = mark == "u_signed" ? "signed" : "unsigned";
        return applyRange(spec, out);
    }
    if (mark == "character") {
        out = vectorType(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, 8);
        out.typeName = "character";
        return true;
    }
    if (mark == "string") {
        // A string is an array of 8-bit character; a use supplies the index range,
        // which `applyRange` turns into an 8-bit-per-element packed vector
        out.kind = VType::ARRAY;
        out.elemp = std::make_shared<VType>(
            vectorType(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, 8));
        out.elemp->typeName = "character";
        out.elemWidth = 8;
        out.kwd = VBasicDTypeKwd::LOGIC;
        out.signing = VSigning::UNSIGNED;
        out.width = 0;
        out.left = 0;
        out.right = 0;
        out.typeName = "string";
        if (spec.hasRange || !spec.constraintMark.empty()) return applyRange(spec, out);
        return true;
    }
    if (mark == "std_logic" || mark == "std_ulogic" || mark == "bit" || mark == "boolean") {
        if (spec.hasRange) {
            flp->v3error("VHDL: range constraint on scalar type '" + mark + "'.");
            return false;
        }
        out.kind = mark == "bit" ? VType::BIT : VType::LOGIC;
        out.dtp = findBasic(flp, mark == "bit" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC);
        out.typeName = mark == "bit" ? "bit" : (mark == "boolean" ? "boolean" : mark);
        return true;
    }
    if (mark == "integer" || mark == "natural" || mark == "positive") {
        out = intType(flp);
        out.intLow = mark == "positive" ? 1 : 0;
        out.intHigh = mark == "integer" ? 0x7fffffff : 0x7fffffff;
        return applyRange(spec, out);
    }
    if (mark == "time") {
        out = timeType(flp);
        return true;
    }
    if (mark == "real") {
        // A scalar handled only at elaboration: it has no width for a constraint to
        // bound, and any use that would need a runtime type (a variable, a port, ...)
        // is rejected where it appears -- see rejectUnmodelledObject.
        out = VType{};
        out.kind = VType::REAL;
        out.dtp = nullptr;
        out.typeName = "real";
        return true;
    }
    if (mark == "text" || mark == "line") {
        // A textio type, existing only inside a subprogram that reads a file at
        // elaboration, so it has no width and no node either
        out = VType{};
        out.kind = mark == "line" ? VType::LINE : VType::FILE;
        out.dtp = nullptr;
        out.typeName = mark;
        return true;
    }
    // The predefined arrays of a scalar element (IEEE 1076-2008 5.3.2.2): a use supplies
    // the index range, as `integer_vector(0 to 1)` does in poc's memory package. The
    // element is the scalar, so indexing yields an integer rather than a packed slice
    if (mark == "integer_vector" || mark == "natural_vector" || mark == "positive_vector"
        || mark == "boolean_vector" || mark == "time_vector") {
        VType elem;
        if (mark == "boolean_vector") {
            elem.kind = VType::BOOLEAN;
            elem.dtp = findBasic(flp, VBasicDTypeKwd::BIT);
            elem.typeName = "boolean";
        } else if (mark == "time_vector") {
            elem = timeType(flp);
        } else {
            elem = intType(flp);
        }
        out = VType{};
        out.kind = VType::ARRAY;
        out.signing = VSigning::NOSIGN;
        out.elemp = std::make_shared<VType>(elem);
        out.elemWidth = elem.width;
        out.typeName = mark;
        return applyRange(spec, out);
    }
    if (isFixedName(mark)) {
        // sfixed and ufixed are vectors whose index range places the binary point; the
        // range must descend (IEEE 1076-2008 fixed_pkg)
        if (!spec.hasRange) {
            unsupportedError(flp, "unconstrained type '" + mark + "'");
            return false;
        }
        out = VType{};
        out.kind = VType::VECTOR;
        out.kwd = VBasicDTypeKwd::LOGIC;
        out.signing
            = mark == "sfixed" || mark == "u_sfixed" ? VSigning::SIGNED : VSigning::UNSIGNED;
        out.elemWidth = 1;
        out.typeName = out.isSigned() ? "sfixed" : "ufixed";
        if (!applyRange(spec, out)) return false;
        if (out.ascending) {
            flp->v3error("VHDL: a fixed-point type needs a descending range.");
            return false;
        }
        return true;
    }
    const bool vectorMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                            || mark == "bit_vector" || mark == "unsigned" || mark == "signed";
    if (!vectorMark) {
        unsupportedError(flp, "type '" + mark + "'");
        return false;
    }
    if (!spec.hasRange && spec.constraintMark.empty()) {
        unsupportedError(flp, "unconstrained type '" + mark + "'");
        return false;
    }
    // The constraint is either a written range or a discrete subtype mark, as in
    // `unsigned(Addr_c)`; `applyRange` resolves both
    out = VType{};
    out.kind = VType::VECTOR;
    out.kwd = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
    out.signing = vectorSigning(mark);
    out.elemWidth = 1;
    out.typeName = mark;
    if (!applyRange(spec, out)) return false;
    return true;
}

}  // namespace vhdlnative
