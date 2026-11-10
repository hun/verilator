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
// SPDX-FileCopyrightText: 2026 Hannes Klas
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
//
// Lowers one analyzed entity/architecture pair into the committed (post-width)
// Verilator AST: the node shapes, dtype canonicalization and re-entry contract
// match what V3VhdlLink expects.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNativeLower.h"

#include "V3Error.h"
#include "V3FileLine.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vhdlnative {

struct VType final {
    enum Kind : uint8_t { LOGIC, BIT, INT, VECTOR } kind = LOGIC;
    AstNodeDType* dtp = nullptr;
    int width = 1;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    int left = 0;
    int right = 0;
    bool ascending = false;

    bool isVector() const { return kind == VECTOR; }
    bool isInt() const { return kind == INT; }
    bool isSigned() const { return signing == VSigning::SIGNED; }
    //! Index of the least significant packed element
    int lowIndex() const { return ascending ? left : right; }
};

class Lowering final {
    AstNetlist* const m_rootp;  // Netlist to emit into
    AstModule* m_modp = nullptr;  // Module being emitted
    struct VarInfo final {
        AstVar* varp = nullptr;
        VType type;
    };
    std::map<std::string, VarInfo> m_scope;  // Signals and ports by folded name
    std::map<std::string, int64_t> m_consts;  // Integer constants by folded name
    std::vector<std::pair<std::string, std::string>> m_pendingNames;  // Duplicate detection
    struct Edge final {
        AstVar* varp = nullptr;
        VEdgeType type = VEdgeType::ET_CHANGED;
    };
    std::vector<Edge> m_edges;  // Clock edges collected while lowering a process
    bool m_initial = false;  // Emitting the time-zero evaluation

    AstNodeDType* findBasic(FileLine* flp, VBasicDTypeKwd kwd) const {
        return m_rootp->typeTablep()->findBasicDType(flp, kwd);
    }
    AstNodeDType* findVector(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing, int left,
                             int right, bool ascending) const {
        const int width = std::abs(left - right) + 1;
        return m_rootp->typeTablep()->findLogicBitDType(
            flp, kwd, VNumRange{left, right, ascending}, width, signing);
    }
    AstNodeDType* findVectorWidth(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing,
                                  int width) const {
        return findVector(flp, kwd, signing, width - 1, 0, false);
    }
    VType vectorType(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing, int width) const {
        VType type;
        type.kind = VType::VECTOR;
        type.width = width;
        type.kwd = kwd;
        type.signing = signing;
        type.left = width - 1;
        type.right = 0;
        type.ascending = false;
        type.dtp = findVectorWidth(flp, kwd, signing, width);
        return type;
    }
    VType intType(FileLine* flp) const {
        VType type;
        type.kind = VType::INT;
        type.width = 32;
        type.dtp = findBasic(flp, VBasicDTypeKwd::INT);
        return type;
    }
    VType logicType(FileLine* flp) const {
        VType type;
        type.kind = VType::LOGIC;
        type.dtp = findBasic(flp, VBasicDTypeKwd::LOGIC);
        return type;
    }
    static VType typeOfDType(AstNodeDType* dtp) {
        VType type;
        type.dtp = dtp;
        type.width = dtp->width();
        const AstBasicDType* const basicp = VN_CAST(dtp->skipRefp(), BasicDType);
        if (!basicp) {
            type.kind = VType::VECTOR;
            return type;
        }
        type.kwd = basicp->keyword();
        if (basicp->keyword() == VBasicDTypeKwd::INT) {
            type.kind = VType::INT;
        } else if (basicp->isRanged() && basicp->width() > 1) {
            type.kind = VType::VECTOR;
            type.left = basicp->left();
            type.right = basicp->right();
            type.ascending = basicp->ascending();
            type.signing = dtp->isSigned() ? VSigning::SIGNED : VSigning::UNSIGNED;
        } else {
            type.kind = basicp->keyword() == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
        }
        return type;
    }

    AstConst* makeIntConst(FileLine* flp, int64_t value) const {
        const VType type = intType(flp);
        V3Number num{flp, type.width, static_cast<uint32_t>(value)};
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(type.dtp);
        return constp;
    }
    //! Constant with one repeated logic value; bit is '0', '1', 'x' or 'z'
    AstConst* makeBitsConst(FileLine* flp, AstNodeDType* dtp, const std::string& bits) const {
        const int width = static_cast<int>(bits.size());
        V3Number num{flp, width, 0u};
        for (int i = 0; i < width; ++i) {
            const char bit = bits[width - 1 - i];  // bits[0] is the most significant bit
            if (bit == '1')
                num.setBit(i, '1');
            else if (bit == 'x' || bit == '-')
                num.setBit(i, 'x');
            else if (bit == 'z')
                num.setBit(i, 'z');
        }
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(dtp);
        return constp;
    }
    AstConst* makeRepeatedConst(FileLine* flp, const VType& type, char bit) const {
        return makeBitsConst(flp, type.dtp, std::string(static_cast<size_t>(type.width), bit));
    }
    AstNodeExpr* makeVectorConst(FileLine* flp, const VType& type, int64_t value) const {
        V3Number num{flp, type.width, static_cast<uint32_t>(value)};
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(type.dtp);
        return constp;
    }
    AstNodeExpr* bitConst(FileLine* flp, char bit) const {
        return makeBitsConst(flp, findBasic(flp, VBasicDTypeKwd::LOGIC), std::string(1, bit));
    }
    AstNodeExpr* resizeExpr(AstNodeExpr* exprp, const VType& to, FileLine* flp) const {
        AstNodeExpr* resultp = exprp;
        const int fromWidth = exprp->width();
        if (fromWidth < to.width) {
            const bool sign = exprp->dtypep()->isSigned();
            resultp = sign ? static_cast<AstNodeExpr*>(new AstExtendS{flp, exprp, to.width})
                           : static_cast<AstNodeExpr*>(new AstExtend{flp, exprp, to.width});
        } else if (fromWidth > to.width) {
            resultp = new AstSel{flp, exprp, 0, to.width};
        }
        resultp->dtypep(to.dtp);
        return resultp;
    }

    bool resolveType(const TypeSpec& spec, VType& out);
    bool evalInt(const Expr& expr, int64_t& value) const;
    bool staticInt(const ExprP& exprp, int64_t& value) const {
        return exprp && evalInt(*exprp, value);
    }

    AstVar* lookupVar(const Expr& nameExpr) const {
        const auto it = m_scope.find(nameExpr.text);
        return it == m_scope.end() ? nullptr : it->second.varp;
    }
    bool isConversion(const std::string& name) const {
        return name == "std_logic_vector" || name == "std_ulogic_vector" || name == "bit_vector"
               || name == "unsigned" || name == "signed" || name == "to_integer"
               || name == "to_unsigned" || name == "to_signed" || name == "integer";
    }

    AstNodeExpr* lowerExpr(const Expr& expr, const VType* context);
    AstNodeExpr* lowerBinary(const Expr& expr, const VType* context);
    AstNodeExpr* lowerConversion(const Expr& expr);
    AstNodeExpr* lowerDesignator(const Expr& expr, bool write);
    AstNodeExpr* lowerSel(const Expr& expr, AstNodeExpr* basep, const VType& baseType, bool write);

    AstNodeExpr* lowerCond(const Expr& expr);
    bool containsEdgeCall(const Expr& expr) const;
    AstNodeExpr* lowerMatching(const Expr& expr, AstNodeExpr* lp, AstNodeExpr* rp);
    AstNodeExpr* lowerAlternatives(const Concurrent& conc, const VType& type);
    AstNode* lowerSelected(const Concurrent& conc);
    bool lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                           std::vector<AstConst*>& cops);
    AstNode* lowerStmt(const Stmt& stmt);
    AstNode* lowerStmtList(const std::vector<StmtP>& stmts);
    AstNode* lowerIf(const Stmt& stmt);
    AstNode* lowerAssign(const Stmt& stmt, bool concurrent);
    void lowerProcess(const Process& process);

public:
    explicit Lowering(AstNetlist* rootp)
        : m_rootp{rootp} {}
    ~Lowering() = default;

    void emit(const EntityUnit& entity, const Architecture& arch);
};

//######################################################################
// Lowering

bool Lowering::evalInt(const Expr& expr, int64_t& value) const {
    switch (expr.kind) {
    case Expr::INT_LIT: value = expr.intValue; return true;
    case Expr::NAME: {
        const auto it = m_consts.find(expr.text);
        if (it == m_consts.end()) return false;
        value = it->second;
        return true;
    }
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
    case Expr::BINARY: {
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
        } else {
            return false;
        }
        return true;
    }
    default: return false;
    }
}

bool Lowering::resolveType(const TypeSpec& spec, VType& out) {
    FileLine* const flp = spec.flp;
    if (!flp) return false;
    const std::string& mark = spec.mark;
    if (mark == "std_logic" || mark == "std_ulogic" || mark == "bit" || mark == "boolean") {
        if (spec.hasRange) {
            flp->v3error("VHDL: range constraint on scalar type '" + mark + "'");
            return false;
        }
        out.kind = mark == "bit" ? VType::BIT : VType::LOGIC;
        out.dtp = findBasic(flp, mark == "bit" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC);
        return true;
    }
    if (mark == "integer" || mark == "natural" || mark == "positive") {
        out = intType(flp);
        if (spec.hasRange) {
            int64_t left = 0;
            int64_t right = 0;
            if (!staticInt(spec.left, left) || !staticInt(spec.right, right)) {
                unsupportedError(flp, "non-static integer subtype range");
                return false;
            }
        }
        return true;
    }
    const bool vectorMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                            || mark == "bit_vector" || mark == "unsigned" || mark == "signed";
    if (!vectorMark) {
        unsupportedError(flp, "type '" + mark + "'");
        return false;
    }
    if (!spec.hasRange) {
        unsupportedError(flp, "unconstrained type '" + mark + "'");
        return false;
    }
    int64_t left = 0;
    int64_t right = 0;
    if (!staticInt(spec.left, left) || !staticInt(spec.right, right)) {
        unsupportedError(flp, "non-static array range");
        return false;
    }
    const VBasicDTypeKwd kwd = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
    const VSigning signing = mark == "signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
    out = vectorType(flp, kwd, signing,
                     std::abs(static_cast<int>(left) - static_cast<int>(right)) + 1);
    out.left = static_cast<int>(left);
    out.right = static_cast<int>(right);
    out.ascending = spec.ascending;
    out.dtp = findVector(flp, kwd, signing, out.left, out.right, out.ascending);
    return true;
}

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
    case Expr::TRUE_LIT: return bitConst(flp, '1');
    case Expr::FALSE_LIT: return bitConst(flp, '0');
    case Expr::CHAR_LIT: {
        const char bit = vhdlLogicBit(expr.charValue);
        if (bit == 0) {
            unsupportedError(flp, std::string{"character literal '"} + expr.charValue
                                      + "' outside a logic context");
            return nullptr;
        }
        if (bit == 'z') {
            unsupportedError(flp, "'Z' literal");
            return nullptr;
        }
        return bitConst(flp, bit);
    }
    case Expr::BITS_LIT: {
        const int width = static_cast<int>(expr.bits.size());
        AstNodeDType* dtp = nullptr;
        if (context && context->isVector() && context->width == width) {
            dtp = context->dtp;
        } else {
            dtp = findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, width);
        }
        return makeBitsConst(flp, dtp, expr.bits);
    }
    case Expr::NAME: {
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) return makeIntConst(flp, constIt->second);
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            flp->v3error("VHDL: unknown identifier '" + expr.spelling + "'");
            return nullptr;
        }
        return new AstVarRef{flp, it->second.varp, VAccess::READ};
    }
    case Expr::PAREN: return lowerExpr(*expr.a, context);
    case Expr::UNARY: {
        AstNodeExpr* operandp = lowerExpr(*expr.a, nullptr);
        if (!operandp) return nullptr;
        const VType type = typeOfDType(operandp->dtypep());
        if (expr.text == "not") {
            AstNot* const notp = new AstNot{flp, operandp};
            notp->dtypep(type.dtp);
            return notp;
        }
        if (expr.text == "+") return operandp;
        if (expr.text == "-") {
            if (!type.isInt()) {
                unsupportedError(flp, "unary '-' on a non-integer value");
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
    case Expr::STRING_LIT: unsupportedError(flp, "string value in an expression"); return nullptr;
    case Expr::BINARY: return lowerBinary(expr, context);
    case Expr::INDEX:
        if (expr.a && expr.a->kind == Expr::NAME && isConversion(expr.a->text)) {
            return lowerConversion(expr);
        }
        return lowerDesignator(expr, false);
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
        if (bit == 0) {
            unsupportedError(flp, "aggregate value");
            return nullptr;
        }
        if (bit == 'z') {
            unsupportedError(flp, "'Z' literal");
            return nullptr;
        }
        return makeRepeatedConst(flp, *context, bit);
    }
    }
    return nullptr;
}

AstNodeExpr* Lowering::lowerBinary(const Expr& expr, const VType* context) {
    FileLine* const flp = expr.flp;
    const std::string& op = expr.text;
    AstNodeExpr* lp = lowerExpr(*expr.a, nullptr);
    if (!lp) return nullptr;
    AstNodeExpr* rp = lowerExpr(*expr.b, nullptr);
    if (!rp) {
        lp->deleteTree();
        return nullptr;
    }
    VType lt = typeOfDType(lp->dtypep());
    VType rt = typeOfDType(rp->dtypep());
    AstNodeDType* const bitDtp = findBasic(flp, VBasicDTypeKwd::LOGIC);

    if (op == "?=" || op == "?/=") {
        if (lt.isInt() != rt.isInt()) {
            unsupportedError(flp, "matching comparison between an integer and a vector");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
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
            if (cmpOp == "=") return new AstEq{flp, lp, rp};
            if (cmpOp == "/=") return new AstNeq{flp, lp, rp};
            if (cmpOp == "<") return new AstLtS{flp, lp, rp};
            if (cmpOp == "<=") return new AstLteS{flp, lp, rp};
            if (cmpOp == ">") return new AstGtS{flp, lp, rp};
            return new AstGteS{flp, lp, rp};
        }
        if (lt.isInt() != rt.isInt()) {
            unsupportedError(flp, "comparison between an integer and a vector");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
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
        }
        if (lt.isInt() || rt.isInt()) {
            unsupportedError(flp, "concatenation with an integer operand");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        const VBasicDTypeKwd kwd = lt.isVector() ? lt.kwd : rt.kwd;
        const VType resultType = vectorType(flp, kwd, VSigning::UNSIGNED, lt.width + rt.width);
        AstConcat* const concatp = new AstConcat{flp, lp, rp};
        concatp->dtypep(resultType.dtp);
        return concatp;
    }

    const bool logical
        = op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor" || op == "xnor";
    if (logical) {
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

    const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/";
    if (!arithmetic) {
        unsupportedError(flp, "operator '" + op + "'");
        lp->deleteTree();
        rp->deleteTree();
        return nullptr;
    }
    if (lt.isInt() && rt.isInt()) {
        AstNodeExpr* resultp = nullptr;
        if (op == "+") {
            resultp = new AstAdd{flp, lp, rp};
        } else if (op == "-") {
            resultp = new AstSub{flp, lp, rp};
        } else if (op == "*") {
            resultp = new AstMul{flp, lp, rp};
        } else {
            resultp = new AstDivS{flp, lp, rp};
        }
        resultp->dtypep(lt.dtp);
        return resultp;
    }
    // A static integer operand acts as a natural/signed literal of the vector operand's type
    if (lt.isInt() != rt.isInt()) {
        const Expr& intExpr = lt.isInt() ? *expr.a : *expr.b;
        int64_t value = 0;
        if (!evalInt(intExpr, value)) {
            unsupportedError(flp, "arithmetic between an integer expression and a vector");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        const VType& vectorT = lt.isInt() ? rt : lt;
        AstNodeExpr* const constp = makeVectorConst(flp, vectorT, value);
        if (lt.isInt()) {
            lp->deleteTree();
            lp = constp;
            lt = vectorT;
        } else {
            rp->deleteTree();
            rp = constp;
            rt = vectorT;
        }
    }
    if (!lt.isVector() || !rt.isVector() || lt.kwd != rt.kwd) {
        unsupportedError(flp, "arithmetic on non-numeric operands");
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
    } else if (op == "/") {
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

AstNodeExpr* Lowering::lowerConversion(const Expr& expr) {
    FileLine* const flp = expr.flp;
    const std::string& name = expr.a->text;
    if (expr.args.size() == 2) {
        if (name == "to_unsigned" || name == "to_signed") {
            AstNodeExpr* valuep = lowerExpr(*expr.args[0], nullptr);
            if (!valuep) return nullptr;
            const VType valueType = typeOfDType(valuep->dtypep());
            int64_t width = 0;
            if (!staticInt(expr.args[1], width) || width <= 0 || width > 4096) {
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
    AstNodeExpr* valuep = lowerExpr(*expr.args[0], nullptr);
    if (!valuep) return nullptr;
    const VType valueType = typeOfDType(valuep->dtypep());
    if (name == "to_integer" || name == "integer") {
        if (valueType.isInt()) return valuep;
        if (!valueType.isVector() || valueType.width > 32) {
            unsupportedError(flp, name + " of this operand");
            valuep->deleteTree();
            return nullptr;
        }
        return resizeExpr(valuep, intType(flp), flp);
    }
    if (!valueType.isVector()) {
        unsupportedError(flp, "conversion of a non-vector value");
        valuep->deleteTree();
        return nullptr;
    }
    VBasicDTypeKwd kwd = valueType.kwd;
    VSigning signing = VSigning::UNSIGNED;
    if (name == "signed") {
        signing = VSigning::SIGNED;
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
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) {
            if (write) {
                flp->v3error("VHDL: cannot assign to constant '" + expr.spelling + "'");
                return nullptr;
            }
            return makeIntConst(flp, constIt->second);
        }
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
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
                && expr.a->kind != Expr::SLICE)) {
            unsupportedError(flp, "selection of a non-name expression");
            return nullptr;
        }
        AstNodeExpr* const basep = lowerDesignator(*expr.a, write);
        if (!basep) return nullptr;
        return lowerSel(expr, basep, typeOfDType(basep->dtypep()), write);
    }
    unsupportedError(flp, "expression as an assignment target");
    return nullptr;
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
    VType elemType;
    elemType.kind = baseType.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
    elemType.dtp = findBasic(flp, baseType.kwd);
    const int lowIndex = baseType.lowIndex();
    if (expr.kind == Expr::INDEX) {
        if (expr.args.size() != 1) {
            unsupportedError(flp, "index with multiple arguments");
            basep->deleteTree();
            return nullptr;
        }
        AstNodeExpr* indexp = lowerExpr(*expr.args[0], nullptr);
        if (!indexp) {
            basep->deleteTree();
            return nullptr;
        }
        if (!typeOfDType(indexp->dtypep()).isInt()) {
            unsupportedError(flp, "non-integer index");
            indexp->deleteTree();
            basep->deleteTree();
            return nullptr;
        }
        AstNodeExpr* offsetp = indexp;
        if (lowIndex != 0) { offsetp = new AstSub{flp, indexp, makeIntConst(flp, lowIndex)}; }
        offsetp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
        AstSel* const selp = new AstSel{flp, basep, offsetp, 1};
        selp->dtypep(elemType.dtp);
        return selp;
    }
    int64_t leftValue = 0;
    int64_t rightValue = 0;
    if (!staticInt(expr.b, leftValue) || !staticInt(expr.c, rightValue)) {
        unsupportedError(flp, "non-static slice bounds");
        basep->deleteTree();
        return nullptr;
    }
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
    const int width = hi - lo + 1;
    const int offset = lo - lowIndex;
    if (offset < 0 || offset + width > baseType.width) {
        flp->v3error("VHDL: slice is out of range of the array");
        basep->deleteTree();
        return nullptr;
    }
    AstSel* const selp = new AstSel{flp, basep, offset, width};
    const VType sliceType = vectorType(flp, baseType.kwd, baseType.signing, width);
    selp->dtypep(sliceType.dtp);
    return selp;
}

AstNodeExpr* Lowering::lowerCond(const Expr& expr) {
    FileLine* const flp = expr.flp;
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        if (expr.args.size() != 1 || expr.args[0]->kind != Expr::NAME) {
            unsupportedError(flp, "edge function argument");
            return nullptr;
        }
        AstVar* const varp = lookupVar(*expr.args[0]);
        if (!varp) {
            flp->v3error("VHDL: unknown signal '" + expr.args[0]->spelling
                         + "' in an edge function");
            return nullptr;
        }
        Edge edge;
        edge.varp = varp;
        edge.type = expr.a->text == "rising_edge" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
        m_edges.push_back(edge);
        // The sensitivity item supplies the edge; at time zero no signal has had an event
        return bitConst(flp, m_initial ? '0' : '1');
    }
    if (containsEdgeCall(expr)) {
        unsupportedError(flp, "clock edge in a compound condition");
        return nullptr;
    }
    AstNodeExpr* condp = lowerExpr(expr, nullptr);
    if (!condp) return nullptr;
    if (condp->width() != 1) {
        flp->v3error("VHDL: condition is not a 1-bit value");
        condp->deleteTree();
        return nullptr;
    }
    return condp;
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
            condp->v3error("VHDL: condition is not a 1-bit value");
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

bool Lowering::lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                                 std::vector<AstConst*>& cops) {
    for (const ExprP& choicep : choices) {
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
                choicep->flp->v3error("VHDL: empty choice range");
                return false;
            }
            for (int64_t value = left;; value += step) {
                AstNodeExpr* const exprp = makeVectorConst(choicep->flp, typeOfDType(dtp), value);
                AstConst* const constp = VN_CAST(exprp, Const);
                cops.push_back(constp);
                if (value == right) break;
            }
            continue;
        }
        AstNodeExpr* const exprp = lowerExpr(*choicep, nullptr);
        if (!exprp) return false;
        AstConst* const constp = VN_CAST(exprp, Const);
        if (!constp) {
            unsupportedError(choicep->flp, "non-static selected-assignment choice");
            exprp->deleteTree();
            return false;
        }
        if (constp->dtypep()->width() != dtp->width()) constp->dtypep(dtp);
        cops.push_back(constp);
    }
    return true;
}

AstNode* Lowering::lowerSelected(const Concurrent& conc) {
    FileLine* const flp = conc.flp;
    AstNodeExpr* const selectorp = lowerExpr(*conc.selector, nullptr);
    if (!selectorp) return nullptr;
    AstNodeExpr* const lhsp = lowerDesignator(*conc.target, true);
    if (!lhsp) {
        selectorp->deleteTree();
        return nullptr;
    }
    const VType ltype = typeOfDType(lhsp->dtypep());
    AstNodeDType* const selDtp = selectorp->dtypep();
    AstCaseItem* itemsp = nullptr;
    AstCaseItem* currentp = nullptr;
    for (const auto& alternative : conc.choices) {
        AstNodeExpr* valuep = lowerExpr(*alternative.second, &ltype);
        if (!valuep) {
            if (itemsp) itemsp->deleteTree();
            lhsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, ltype, flp);
        AstNode* const assignp = new AstAssignW{flp, lhsp->cloneTree(false), valuep};
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(alternative.first, selDtp, cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            assignp->deleteTree();
            itemsp->deleteTree();
            lhsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        if (cops.empty() && currentp) {
            // A second "others" alternative can never be selected
            assignp->deleteTree();
            continue;
        }
        AstCaseItem* const itemp = cops.empty() ? new AstCaseItem{flp, nullptr, assignp}
                                                : new AstCaseItem{flp, cops.front(), assignp};
        for (size_t i = 1; i < cops.size(); ++i) itemp->addCondsp(cops[i]);
        if (cops.empty()) currentp = itemp;
        itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, itemp) : itemp;
    }
    lhsp->deleteTree();
    if (!itemsp) {
        selectorp->deleteTree();
        return nullptr;
    }
    return new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
}

AstNode* Lowering::lowerAssign(const Stmt& stmt, bool concurrent) {
    AstNodeExpr* lhsp = lowerDesignator(*stmt.target, true);
    if (!lhsp) return nullptr;
    if (!concurrent && !VN_IS(lhsp, VarRef)) {
        unsupportedError(stmt.flp, "assignment to a slice or element from a process");
        lhsp->deleteTree();
        return nullptr;
    }
    if (!concurrent) {
        // V3Tristate/V3Inst model inout ports as continuously driven; a procedural
        // assignment would reach DFG as a non-read reference
        bool inoutPort = false;
        lhsp->foreach([&](AstVarRef* refp) {
            if (refp->varp()->isIO() && refp->varp()->direction().isInout()) inoutPort = true;
        });
        if (inoutPort) {
            unsupportedError(stmt.flp, "procedural assignment to an inout port");
            lhsp->deleteTree();
            return nullptr;
        }
    }
    const VType ltype = typeOfDType(lhsp->dtypep());
    AstNodeExpr* rhsp = lowerExpr(*stmt.value, &ltype);
    if (!rhsp) {
        lhsp->deleteTree();
        return nullptr;
    }
    rhsp = resizeExpr(rhsp, ltype, stmt.flp);
    if (concurrent) return new AstAssignW{stmt.flp, lhsp, rhsp};
    if (m_initial) return new AstAssign{stmt.flp, lhsp, rhsp};
    return new AstAssignDly{stmt.flp, lhsp, rhsp};
}

AstNode* Lowering::lowerIf(const Stmt& stmt) {
    AstNode* elsesp = nullptr;
    for (std::size_t i = stmt.branches.size(); i-- > 0;) {
        const auto& branch = stmt.branches[i];
        AstNode* bodyp = nullptr;
        for (const StmtP& stmtp : branch.second) {
            AstNode* const nodep = lowerStmt(*stmtp);
            if (nodep) bodyp = bodyp ? AstNode::addNext<AstNode, AstNode>(bodyp, nodep) : nodep;
        }
        if (!branch.first) {
            elsesp = bodyp;
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

AstNode* Lowering::lowerStmt(const Stmt& stmt) {
    if (stmt.kind == Stmt::ASSIGN) return lowerAssign(stmt, false);
    if (stmt.kind == Stmt::IF) return lowerIf(stmt);
    return nullptr;
}

AstNode* Lowering::lowerStmtList(const std::vector<StmtP>& stmts) {
    AstNode* headp = nullptr;
    for (const StmtP& stmtp : stmts) {
        AstNode* const nodep = lowerStmt(*stmtp);
        if (nodep) headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
    }
    return headp;
}

void Lowering::lowerProcess(const Process& process) {
    if (!process.hasSensitivity) {
        unsupportedError(process.flp, "process without a sensitivity list (wait-based processes)");
        return;
    }
    std::vector<AstVar*> sensVars;
    for (const std::string& name : process.sensitivity) {
        const auto it = m_scope.find(name);
        if (it == m_scope.end()) {
            process.flp->v3error("VHDL: unknown signal '" + name + "' in a sensitivity list");
            return;
        }
        sensVars.push_back(it->second.varp);
    }

    m_edges.clear();
    m_initial = false;
    AstNode* const bodyp = lowerStmtList(process.body);
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    m_initial = true;
    AstNode* const initp = lowerStmtList(process.body);
    m_initial = false;

    bool bad = false;
    for (const Edge& edge : edges) {
        if (std::find(sensVars.begin(), sensVars.end(), edge.varp) == sensVars.end()) {
            unsupportedError(process.flp, "clock edge on a signal outside the sensitivity list");
            bad = true;
        }
    }
    if (bad) {
        if (bodyp) bodyp->deleteTree();
        if (initp) initp->deleteTree();
        return;
    }

    AstSenItem* itemsp = nullptr;
    for (AstVar* const varp : sensVars) {
        VEdgeType type = VEdgeType::ET_CHANGED;
        for (const Edge& edge : edges) {
            if (edge.varp == varp) type = edge.type;
        }
        AstSenItem* const itemp
            = new AstSenItem{process.flp, type, new AstVarRef{process.flp, varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    if (bodyp) {
        m_modp->addStmtsp(new AstAlways{process.flp, VAlwaysKwd::ALWAYS,
                                        new AstSenTree{process.flp, itemsp}, bodyp});
    } else if (itemsp) {
        itemsp->deleteTree();
    }
    // VHDL executes a process once at time zero, before any sensitivity event, and the
    // initial combination is visible to the testbench after the first eval
    if (initp) m_modp->addStmtsp(new AstInitialStatic{process.flp, initp});
}

void Lowering::emit(const EntityUnit& entity, const Architecture& arch) {
    m_modp = new AstModule{entity.flp, entity.spelling, entity.libname};
    m_modp->inLibrary(entity.inLibrary || arch.inLibrary);

    int pinNum = 0;
    for (const PortDecl& port : entity.ports) {
        VType type;
        if (!resolveType(port.type, type)) continue;
        for (const auto& name : port.names) {
            if (!m_scope.emplace(name.first, VarInfo{}).second) {
                port.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                continue;
            }
            AstVar* const varp = new AstVar{port.flp, VVarType::PORT, name.second, type.dtp};
            varp->direction(port.dir == PortMode::IN    ? VDirection::INPUT
                            : port.dir == PortMode::OUT ? VDirection::OUTPUT
                                                        : VDirection::INOUT);
            varp->pinNum(++pinNum);
            // VHDL ports are signals, so procedural assignment is legal
            varp->declTyped(true);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
        }
    }

    for (const Decl& decl : arch.decls) {
        if (decl.kind == Decl::CONSTANT) {
            if (!decl.init) {
                decl.flp->v3error("VHDL: constant without a value");
                continue;
            }
            int64_t value = 0;
            if (!evalInt(*decl.init, value)) {
                unsupportedError(decl.flp,
                                 "constant value that is not a static integer expression");
                continue;
            }
            for (const auto& name : decl.names) {
                if (m_scope.count(name.first) || m_consts.count(name.first)) {
                    decl.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                    continue;
                }
                m_consts[name.first] = value;
            }
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            if (m_scope.count(name.first) || m_consts.count(name.first)) {
                decl.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                continue;
            }
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, name.second, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.init) {
                AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
                AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
                if (!rhsp) {
                    lhsp->deleteTree();
                    continue;
                }
                rhsp = resizeExpr(rhsp, type, decl.flp);
                m_modp->addStmtsp(
                    new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
            }
        }
    }

    for (const auto& stmtp : arch.stmts) {
        if (stmtp->kind == Concurrent::PROCESS) {
            lowerProcess(*stmtp->process);
            continue;
        }
        if (stmtp->kind == Concurrent::SEL_ASSIGN) {
            AstNode* const casep = lowerSelected(*stmtp);
            if (!casep) continue;
            m_modp->addStmtsp(new AstAlways{stmtp->flp, VAlwaysKwd::CONT_ASSIGN, nullptr, casep});
            continue;
        }
        AstNodeExpr* const lhsp = lowerDesignator(*stmtp->target, true);
        if (!lhsp) continue;
        const VType ltype = typeOfDType(lhsp->dtypep());
        AstNodeExpr* rhsp = nullptr;
        if (stmtp->kind == Concurrent::COND_ASSIGN) {
            rhsp = lowerAlternatives(*stmtp, ltype);
        } else {
            rhsp = lowerExpr(*stmtp->value, &ltype);
            if (rhsp) rhsp = resizeExpr(rhsp, ltype, stmtp->flp);
        }
        if (!rhsp) {
            lhsp->deleteTree();
            continue;
        }
        m_modp->addStmtsp(new AstAlways{stmtp->flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                                        new AstAssignW{stmtp->flp, lhsp, rhsp}});
    }

    m_rootp->addModulesp(m_modp);
    m_modp = nullptr;
    m_scope.clear();
    m_consts.clear();
}

//######################################################################
// Entry point

void lower(const EntityUnit& entity, const Architecture& arch, AstNetlist* rootp) {
    Lowering lowering{rootp};
    lowering.emit(entity, arch);
}

}  // namespace vhdlnative
