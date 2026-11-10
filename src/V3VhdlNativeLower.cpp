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

struct VType;
using VTypeP = std::shared_ptr<VType>;

//! One record element: its bit offset from the least significant bit of the record
struct VField final {
    std::string name;  // Folded
    int offset = 0;
    int width = 1;
    VTypeP type;
};

//! VHDL type as it survives into the committed AST
struct VType final {
    enum Kind : uint8_t { LOGIC, BIT, BOOLEAN, INT, VECTOR, ARRAY, RECORD, ENUM } kind = LOGIC;
    AstNodeDType* dtp = nullptr;
    int width = 1;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    int left = 0;
    int right = 0;
    bool ascending = false;
    int intLow = 0;  // INTEGER subtype bounds
    int intHigh = 0;
    VTypeP elemp;  // ARRAY: element type
    int elemWidth = 1;  // ARRAY: width of one element
    std::vector<VField> fields;  // RECORD: most significant element first
    std::vector<std::string> literals;  // ENUM: literal names in declaration order

    bool isVector() const { return kind == VECTOR || kind == ARRAY; }
    bool isInt() const { return kind == INT; }
    bool isSigned() const { return signing == VSigning::SIGNED; }
    //! Number of elements of an array, or 1 for a scalar
    int length() const { return isVector() ? std::abs(left - right) + 1 : 1; }
    //! Index of the least significant packed element
    int lowIndex() const { return ascending ? left : right; }
    //! Bit offset of a declared array index from the least significant element
    int indexOffset(int index) const { return index - lowIndex(); }
};

//! Elaborated contents of one package, publishable to the units that use it
struct PackageInfo final {
    std::map<std::string, VType> types;
    std::map<std::string, int64_t> consts;
    std::map<std::string, AstConst*> constValues;
    std::map<std::string, VType> constTypes;
    std::map<std::string, AstConst*> enumConsts;
    std::vector<std::unique_ptr<AstConst>> owned;  // Owns the constants above
};

struct PackageTable::Impl final {
    std::map<std::string, PackageInfo> packages;  // Folded package name -> contents
};

PackageTable::PackageTable()
    : m_implp{new Impl} {}
PackageTable::~PackageTable() = default;

class Lowering final {
    AstNetlist* const m_rootp;  // Netlist to emit into
    AstModule* m_modp = nullptr;  // Module being emitted
    struct VarInfo final {
        AstVar* varp = nullptr;
        VType type;
    };
    std::map<std::string, VarInfo> m_scope;  // Signals and ports by folded name
    std::map<std::string, int64_t> m_consts;  // Integer constants by folded name
    std::map<std::string, VType> m_types;  // Declared types by folded name
    std::map<std::string, AstConst*> m_constValues;  // Typed constants by folded name
    std::map<std::string, VType> m_constTypes;  // Type of each typed constant
    std::map<std::string, AstConst*> m_enumConsts;  // Enumeration literals by folded name
    std::vector<std::unique_ptr<AstConst>> m_ownedConsts;  // Owns constants in the tables
    std::vector<std::pair<std::string, std::string>> m_pendingNames;  // Duplicate detection
    struct Edge final {
        AstVar* varp = nullptr;
        VEdgeType type = VEdgeType::ET_CHANGED;
    };
    std::vector<Edge> m_edges;  // Clock edges collected while lowering a process
    bool m_initial = false;  // Emitting the time-zero evaluation
    //! Active loop, with the flags its exit/next conditions latch
    struct LoopFrame final {
        AstVar* exitp = nullptr;  // Set when an exit condition fired
        AstVar* nextp = nullptr;  // Set when a next condition fired
        std::string label;
        bool guard = false;  // The body uses exit/next, so statements need guarding
    };
    std::vector<LoopFrame> m_loops;
    int m_loopSeq = 0;  // Disambiguates the loop flag variables
    PackageTable::Impl* m_publishp = nullptr;  // Package table to publish into
    const PackageTable::Impl* m_packagesp = nullptr;  // Packages visible to this unit
    std::set<std::string> m_imported;  // Names that came from a used package

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
    void registerType(const TypeDecl& decl);
    void emitPackage(const PackageUnit& pkg);
    void importPackage(const std::string& name);
    void emitConstant(const Decl& decl, bool deferredOk);
    bool reserveName(const std::string& folded, const std::string& spelling, FileLine* flp);
    AstNodeDType* enumDType(FileLine* flp, const TypeDecl& decl, int width);
    bool applyRange(const TypeSpec& spec, VType& type);
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
    AstNodeExpr* sliceOf(FileLine* flp, AstNodeExpr* basep, const VType& baseType, int elemWidth,
                         int hi, int lo);

    AstNode* lowerCase(const Stmt& stmt);
    AstNode* lowerFor(const Stmt& stmt);
    static bool usesExitNext(const std::vector<StmtP>& stmts);
    AstNodeExpr* loopGuard(FileLine* flp) const;
    AstNode* lowerWhile(const Stmt& stmt);
    AstVar* newLoopFlag(FileLine* flp, const char* what);
    LoopFrame* findLoop(const std::string& label);
    AstNode* clearLoopFlag(FileLine* flp, AstVar* flagp);
    AstNode* setLoopFlag(FileLine* flp, AstVar* flagp, AstNodeExpr* condp);
    AstNodeExpr* lowerCond(const Expr& expr);
    bool containsEdgeCall(const Expr& expr) const;
    AstNodeExpr* lowerMatching(const Expr& expr, AstNodeExpr* lp, AstNodeExpr* rp);
    AstNodeExpr* lowerAttr(const Expr& expr);
    AstNodeExpr* lowerAggregate(const Expr& expr, const VType& type);
    AstConst* foldToConst(AstNodeExpr* exprp);
    AstNodeExpr* lowerSelectedExpr(const Expr& expr);
    const VType* typeOfName(const Expr& expr) const;
    bool typeOfDesignator(const Expr& expr, VType& out) const;
    VType typeOfTarget(const Expr& target, AstNodeExpr* lhsp) const;
    AstNodeExpr* lowerAlternatives(const Concurrent& conc, const VType& type);
    AstNode* lowerSelected(const Concurrent& conc);
    bool lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                           std::vector<AstConst*>& cops);
    AstNode* lowerStmt(const Stmt& stmt);
    AstNode* lowerStmtList(const std::vector<StmtP>& stmts);
    AstNode* lowerIf(const Stmt& stmt);
    AstNode* lowerAssign(const Stmt& stmt, bool concurrent, bool variable = false);
    void lowerProcess(const Process& process);

public:
    explicit Lowering(AstNetlist* rootp, const PackageTable::Impl* packagesp = nullptr)
        : m_rootp{rootp}
        , m_packagesp{packagesp} {}
    ~Lowering() = default;

    void emit(const EntityUnit& entity, const Architecture& arch);
    void emitPackage(const PackageUnit& pkg, PackageTable::Impl& table);
};

//######################################################################
// Lowering

bool Lowering::evalInt(const Expr& expr, int64_t& value) const {
    switch (expr.kind) {
    case Expr::INT_LIT: value = expr.intValue; return true;
    case Expr::NAME: {
        const auto it = m_consts.find(expr.text);
        if (it != m_consts.end()) {
            value = it->second;
            return true;
        }
        const auto enumIt = m_enumConsts.find(expr.text);
        if (enumIt != m_enumConsts.end()) {
            value = static_cast<int64_t>(enumIt->second->num().toUInt());
            return true;
        }
        return false;
    }
    case Expr::ATTR: {
        // Attributes of a type mark or name whose value is known statically
        if (!expr.a || expr.a->kind != Expr::NAME) return false;
        const VType* typep = typeOfName(*expr.a);
        if (!typep) {
            const auto typeIt = m_types.find(expr.a->text);
            if (typeIt == m_types.end()) return false;
            typep = &typeIt->second;
        }
        const std::string& attribute = expr.text;
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
            value = position;
            return true;
        }
        if (typep->kind != VType::ENUM) return false;
        const int64_t count = static_cast<int64_t>(typep->literals.size());
        if (attribute == "succ") {
            value = (position + 1) % count;
            return true;
        }
        if (attribute == "pred") {
            value = (position - 1 + count) % count;
            return true;
        }
        return false;
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
    case TypeDecl::ENUM: {
        int width = 1;
        while ((1 << width) < static_cast<int>(decl.literals.size())) ++width;
        type.kind = VType::ENUM;
        type.width = width;
        type.dtp = enumDType(flp, decl, width);
        for (const auto& literal : decl.literals) type.literals.push_back(literal.first);
        for (size_t i = 0; i < decl.literals.size(); ++i) {
            AstConst* const constp
                = new AstConst{flp, V3Number{flp, width, static_cast<uint32_t>(i)}};
            constp->dtypep(type.dtp);
            m_enumConsts[decl.literals[i].first] = constp;
        }
        break;
    }
    case TypeDecl::ARRAY: {
        VType elem;
        if (!resolveType(decl.base, elem)) return;
        if (!decl.index.hasRange) {
            unsupportedError(flp, "array type without an index constraint");
            return;
        }
        type.kind = VType::ARRAY;
        type.elemp = std::make_shared<VType>(elem);
        type.elemWidth = elem.width;
        type.kwd = elem.kwd;
        type.signing = VSigning::UNSIGNED;
        int64_t left = 0;
        int64_t right = 0;
        if (!staticInt(decl.index.left, left) || !staticInt(decl.index.right, right)) {
            // An unconstrained array type: every use must supply the index constraint
            type.left = 0;
            type.right = 0;
            type.width = 0;
            break;
        }
        type.left = static_cast<int>(left);
        type.right = static_cast<int>(right);
        type.ascending = decl.index.ascending;
        type.width = elem.width * type.length();
        type.dtp = findVectorWidth(flp, elem.kwd, VSigning::UNSIGNED, type.width);
        break;
    }
    case TypeDecl::RECORD: {
        int total = 0;
        VBasicDTypeKwd kwd = VBasicDTypeKwd::BIT;
        std::vector<VField> fields;
        for (const TypeDecl::Member& member : decl.members) {
            VType memberType;
            if (!resolveType(member.type, memberType)) return;
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
        type.fields = std::move(fields);
        type.dtp = findVectorWidth(flp, kwd, VSigning::UNSIGNED, total);
        break;
    }
    }
    m_types[decl.name] = type;
}

bool Lowering::applyRange(const TypeSpec& spec, VType& type) {
    FileLine* const flp = spec.flp;
    if (spec.hasRange) {
        int64_t left = 0;
        int64_t right = 0;
        if (!staticInt(spec.left, left) || !staticInt(spec.right, right)) {
            unsupportedError(flp, "non-static range constraint");
            return false;
        }
        type.left = static_cast<int>(left);
        type.right = static_cast<int>(right);
        type.ascending = spec.ascending;
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
        const auto it = m_types.find(spec.constraintMark);
        if (it == m_types.end()) {
            flp->v3error("VHDL: unknown subtype '" + spec.constraintMark + "'");
            return false;
        }
        if (it->second.kind == VType::INT) {
            type.left = it->second.left;
            type.right = it->second.right;
            type.ascending = it->second.ascending;
            type.intLow = it->second.intLow;
            type.intHigh = it->second.intHigh;
            return true;
        }
        if (type.isVector()) {
            type.left = it->second.left;
            type.right = it->second.right;
            type.ascending = it->second.ascending;
            type.width = type.elemWidth * type.length();
            type.dtp = type.kind == VType::ARRAY
                           ? findVectorWidth(flp, type.kwd, type.signing, type.width)
                           : findVector(flp, type.kwd, type.signing, type.left, type.right,
                                        type.ascending);
            return true;
        }
        return true;
    }
    return true;
}

bool Lowering::resolveType(const TypeSpec& spec, VType& out) {
    FileLine* const flp = spec.flp;
    if (!flp) return false;
    const std::string& mark = spec.mark;
    const auto declared = m_types.find(mark);
    if (declared != m_types.end()) {
        out = declared->second;
        return applyRange(spec, out);
    }
    if (mark == "u_unsigned" || mark == "u_signed") {
        // An unconstrained numeric_std vector type; the use site supplies the index range
        if (!spec.hasRange && spec.constraintMark.empty()) {
            unsupportedError(flp, "unconstrained array type mark without an index constraint");
            return false;
        }
        out = vectorType(flp, VBasicDTypeKwd::LOGIC,
                         mark == "u_signed" ? VSigning::SIGNED : VSigning::UNSIGNED, 1);
        return applyRange(spec, out);
    }
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
        out.intLow = mark == "positive" ? 1 : 0;
        out.intHigh = mark == "integer" ? 0x7fffffff : 0x7fffffff;
        return applyRange(spec, out);
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
        const auto valueIt = m_constValues.find(expr.text);
        if (valueIt != m_constValues.end()) return valueIt->second->cloneTree(false);
        const auto enumIt = m_enumConsts.find(expr.text);
        if (enumIt != m_enumConsts.end()) return enumIt->second->cloneTree(false);
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            flp->v3error("VHDL: unknown identifier '" + expr.spelling + "'");
            return nullptr;
        }
        return new AstVarRef{flp, it->second.varp, VAccess::READ};
    }
    case Expr::AGG_POSITIONAL:
    case Expr::AGG_NAMED: {
        if (!context) {
            unsupportedError(flp, "aggregate without a known target type");
            return nullptr;
        }
        return lowerAggregate(expr, *context);
    }
    case Expr::ATTR: return lowerAttr(expr);
    case Expr::SELECTED: return lowerSelectedExpr(expr);
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
        if (bit != 0) {
            if (bit == 'z') {
                unsupportedError(flp, "'Z' literal");
                return nullptr;
            }
            return makeRepeatedConst(flp, *context, bit);
        }
        // An arbitrary value is replicated across every element
        if (!context->isVector()) {
            unsupportedError(flp, "aggregate value for a non-array type");
            return nullptr;
        }
        VType elemType;
        if (context->kind == VType::ARRAY && context->elemp) {
            elemType = *context->elemp;
        } else {
            elemType.kind = context->kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
            elemType.dtp = findBasic(flp, context->kwd);
        }
        AstNodeExpr* valuep = lowerExpr(*expr.a, &elemType);
        if (!valuep) return nullptr;
        valuep = resizeExpr(valuep, elemType, flp);
        const int count = context->length();
        AstNodeExpr* resultp = valuep;
        for (int i = 1; i < count; ++i) {
            resultp = new AstConcat{flp, resultp, valuep->cloneTree(false)};
        }
        resultp->dtypep(context->dtp);
        return resultp;
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
            // numeric_std compares a vector with an integer of the same value
            const Expr& intExpr = lt.isInt() ? *expr.a : *expr.b;
            int64_t value = 0;
            if (!evalInt(intExpr, value)) {
                unsupportedError(flp, "comparison between an integer expression and a vector");
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
                && expr.a->kind != Expr::SLICE && expr.a->kind != Expr::SELECTED)) {
            unsupportedError(flp, "selection of a non-name expression");
            return nullptr;
        }
        AstNodeExpr* const basep = lowerDesignator(*expr.a, write);
        if (!basep) return nullptr;
        // Prefer the declared type: it keeps the element width of an array type
        VType baseType;
        if (!typeOfDesignator(*expr.a, baseType)) baseType = typeOfDType(basep->dtypep());
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
    // An index expression that is an attribute ('range) names the prefix's own bounds
    const Expr* rangep = nullptr;
    if (expr.kind == Expr::INDEX && expr.args.size() == 1 && expr.args[0]->kind == Expr::ATTR) {
        rangep = expr.args[0].get();
    } else if (expr.kind == Expr::SLICE && expr.b && expr.b->kind == Expr::ATTR) {
        rangep = expr.b.get();
    }
    if (rangep) {
        if (rangep->text != "range" || !rangep->a || rangep->a->kind != Expr::NAME) {
            unsupportedError(flp, "attribute '" + rangep->text + "' as an index");
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
        int hi = prefixTypep->ascending ? prefixTypep->right : prefixTypep->left;
        int lo = prefixTypep->ascending ? prefixTypep->left : prefixTypep->right;
        if (hi < lo) std::swap(hi, lo);
        return sliceOf(flp, basep, baseType, elemWidth, hi, lo);
    }
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
        const int lowIndex = baseType.lowIndex();
        // The least significant element is the lowest index, in either direction
        AstNodeExpr* offsetp = indexp;
        if (lowIndex != 0) { offsetp = new AstSub{flp, indexp, makeIntConst(flp, lowIndex)}; }
        offsetp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
        if (elemWidth != 1) {
            AstNodeExpr* const scaledp = new AstMul{flp, offsetp, makeIntConst(flp, elemWidth)};
            scaledp->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
            offsetp = scaledp;
        }
        AstSel* const selp = new AstSel{flp, basep, offsetp, elemWidth};
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
    return sliceOf(flp, basep, baseType, elemWidth, hi, lo);
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

//! Type of a prefix name, or null when the name is not a signal or constant
//! Type of a designator (name, selected name, index or slice) when it is known
bool Lowering::typeOfDesignator(const Expr& expr, VType& out) const {
    switch (expr.kind) {
    case Expr::NAME: {
        const VType* const typep = typeOfName(expr);
        if (!typep) return false;
        out = *typep;
        return true;
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
        if (expr.kind == Expr::INDEX) {
            if (prefixType.kind == VType::ARRAY && prefixType.elemp) {
                out = *prefixType.elemp;
            } else {
                out = VType{};
                out.kind = prefixType.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
                out.dtp = findBasic(expr.flp, prefixType.kwd);
            }
            return true;
        }
        // A slice keeps the element type over the sliced index range
        if (!expr.b || !expr.c) return false;
        int64_t left = 0;
        int64_t right = 0;
        if (!staticInt(expr.b, left) || !staticInt(expr.c, right)) return false;
        out = prefixType;
        out.left = static_cast<int>(left);
        out.right = static_cast<int>(right);
        out.ascending = expr.text == "to";
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
    const auto it = m_scope.find(expr.text);
    if (it != m_scope.end()) return &it->second.type;
    const auto constIt = m_constTypes.find(expr.text);
    if (constIt != m_constTypes.end()) return &constIt->second;
    return nullptr;
}

AstNodeExpr* Lowering::lowerAttr(const Expr& expr) {
    FileLine* const flp = expr.flp;
    if (!expr.a || expr.a->kind != Expr::NAME) {
        unsupportedError(flp, "attribute of a non-name prefix");
        return nullptr;
    }
    const std::string& attribute = expr.text;
    if (attribute == "event" || attribute == "stable" || attribute == "active"
        || attribute == "last_event" || attribute == "last_active") {
        unsupportedError(flp, "signal attribute '" + attribute + "'");
        return nullptr;
    }
    // The prefix is either a declared name or a type mark
    const VType* typep = typeOfName(*expr.a);
    bool typeMark = false;
    if (!typep) {
        const auto typeIt = m_types.find(expr.a->text);
        if (typeIt != m_types.end()) {
            typep = &typeIt->second;
            typeMark = true;
        }
    }
    if (!typep) {
        unsupportedError(flp, "attribute '" + attribute + "' of an unresolved name");
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
        argp = resizeExpr(argp, *typep, flp);
        // A static argument gives a constant result, which constants may use
        const AstConst* const argConstp = VN_CAST(argp, Const);
        const int position = argConstp ? static_cast<int>(argConstp->num().toUInt()) : 0;
        if (attribute == "pos" || attribute == "to_integer") {
            if (argConstp) {
                argp->deleteTree();
                return makeIntConst(flp, position);
            }
            return resizeExpr(argp, intType(flp), flp);
        }
        if (attribute == "val") {
            if (argConstp) {
                argp->deleteTree();
                return enumConst(position);
            }
            return resizeExpr(argp, *typep, flp);
        }
        if ((attribute == "succ" || attribute == "pred") && argConstp) {
            argp->deleteTree();
            const int stepped
                = attribute == "succ" ? (position + 1) % count : (position - 1 + count) % count;
            return enumConst(stepped);
        }
        if (attribute == "succ" || attribute == "pred") {
            // succ/pred step to the neighbouring literal and wrap at the ends
            const bool succ = attribute == "succ";
            AstNodeExpr* const atLimitp
                = new AstEq{flp, argp->cloneTree(false), enumConst(succ ? count - 1 : 0)};
            atLimitp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            AstNodeExpr* const steppedp
                = succ ? static_cast<AstNodeExpr*>(new AstAdd{flp, argp, enumConst(1)})
                       : static_cast<AstNodeExpr*>(new AstSub{flp, argp, enumConst(1)});
            steppedp->dtypep(enumDtp);
            AstCond* const resultp
                = new AstCond{flp, atLimitp, enumConst(succ ? 0 : count - 1), steppedp};
            resultp->dtypep(enumDtp);
            return resultp;
        }
        unsupportedError(flp, "enumeration attribute '" + attribute + "'");
        return nullptr;
    }
    if (!expr.args.empty()) {
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
                               int elemWidth, int hi, int lo) {
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
    AstSel* const selp = new AstSel{flp, basep, offset, width};
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
    selp->dtypep(sliceType.dtp);
    return selp;
}

AstNodeExpr* Lowering::lowerSelectedExpr(const Expr& expr) {
    FileLine* const flp = expr.flp;
    if (!expr.a) {
        unsupportedError(flp, "selected name without a prefix");
        return nullptr;
    }
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

//! Fold an expression built only from constants into one AstConst, or return null
AstConst* Lowering::foldToConst(AstNodeExpr* exprp) {
    if (!exprp) return nullptr;
    if (AstConst* const constp = VN_CAST(exprp, Const)) return constp;
    AstConcat* const concatp = VN_CAST(exprp, Concat);
    if (!concatp) return nullptr;
    AstConst* const lhp = foldToConst(concatp->lhsp()->unlinkFrBack());
    AstConst* const rhp = foldToConst(concatp->rhsp()->unlinkFrBack());
    if (!lhp || !rhp) {
        if (lhp) lhp->deleteTree();
        if (rhp) rhp->deleteTree();
        return nullptr;
    }
    V3Number num{exprp->fileline(), lhp->num().width() + rhp->num().width(), 0u};
    num.opConcat(lhp->num(), rhp->num());
    AstConst* const constp = new AstConst{exprp->fileline(), num};
    constp->dtypep(exprp->dtypep());
    lhp->deleteTree();
    rhp->deleteTree();
    VL_DO_DANGLING(exprp->deleteTree(), exprp);
    return constp;
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
            // Named association: each choice names one record element
            for (size_t i = 0; i < expr.args.size(); ++i) {
                for (const ExprP& choicep : expr.choices[i]) {
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
                    if (!staticInt(choicep->b, left) || !staticInt(choicep->c, right)) {
                        unsupportedError(flp, "non-static aggregate choice range");
                        return nullptr;
                    }
                    const int offsetLeft = type.indexOffset(static_cast<int>(left));
                    const int offsetRight = type.indexOffset(static_cast<int>(right));
                    Segment segment;
                    segment.offset = std::min(offsetLeft, offsetRight) * elemWidth;
                    segment.width = (std::abs(offsetLeft - offsetRight) + 1) * elemWidth;
                    segment.exprp = expr.args[i].get();
                    segments.push_back(segment);
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
    // Fill the elements no association names, in element-sized pieces
    const int elemSpan = type.kind == VType::RECORD ? 1 : std::max(1, type.elemWidth);
    const int elementCount = elemSpan > 0 ? type.width / elemSpan : 0;
    if (type.kind != VType::RECORD && !othersValuep) {
        // No `others`: every element must be covered by an association
    } else if (othersValuep) {
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
    // Assemble from the least significant segment up
    AstNodeExpr* resultp = nullptr;
    V3Number constValue{flp, type.width, 0u};  // Valid when every segment is constant
    bool allConst = true;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const Segment& segment = segments[i];
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
        AstNodeExpr* valuep = lowerExpr(*segment.exprp, &segType);
        if (!valuep) {
            if (resultp) resultp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, segType, flp);
        if (AstConst* const constp = VN_CAST(valuep, Const)) {
            for (int bit = 0; bit < segment.width; ++bit) {
                char value = '0';
                if (constp->num().bitIs1(bit)) {
                    value = '1';
                } else if (constp->num().bitIsXZ(bit)) {
                    value = 'x';
                }
                constValue.setBit(segment.offset + bit, value);
            }
            VL_DO_DANGLING(valuep->deleteTree(), valuep);
            continue;
        }
        // A non-static segment: build the value with concatenations instead
        allConst = false;
        resultp
            = resultp ? static_cast<AstNodeExpr*>(new AstConcat{flp, valuep, resultp}) : valuep;
    }
    if (allConst) {
        AstConst* const constp = new AstConst{flp, constValue};
        constp->dtypep(type.dtp);
        return constp;
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
    const VType ltype = typeOfTarget(*conc.target, lhsp);
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

AstNode* Lowering::lowerAssign(const Stmt& stmt, bool concurrent, bool variable) {
    AstNodeExpr* lhsp = lowerDesignator(*stmt.target, true);
    if (!lhsp) return nullptr;
    if (!concurrent && !variable) {
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
    const VType ltype = typeOfTarget(*stmt.target, lhsp);
    AstNodeExpr* rhsp = lowerExpr(*stmt.value, &ltype);
    if (!rhsp) {
        lhsp->deleteTree();
        return nullptr;
    }
    rhsp = resizeExpr(rhsp, ltype, stmt.flp);
    if (concurrent) return new AstAssignW{stmt.flp, lhsp, rhsp};
    // A variable assignment updates its target immediately
    if (variable || m_initial) return new AstAssign{stmt.flp, lhsp, rhsp};
    return new AstAssignDly{stmt.flp, lhsp, rhsp};
}

AstNode* Lowering::lowerIf(const Stmt& stmt) {
    AstNode* elsesp = nullptr;
    for (std::size_t i = stmt.branches.size(); i-- > 0;) {
        const auto& branch = stmt.branches[i];
        AstNode* bodyp = lowerStmtList(branch.second);
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
    if (stmt.kind == Stmt::VAR_ASSIGN) return lowerAssign(stmt, false, true);
    if (stmt.kind == Stmt::IF) return lowerIf(stmt);
    if (stmt.kind == Stmt::CASE) return lowerCase(stmt);
    if (stmt.kind == Stmt::FOR) return lowerFor(stmt);
    if (stmt.kind == Stmt::WHILE || stmt.kind == Stmt::LOOP) return lowerWhile(stmt);
    // exit/next are handled by lowerStmtList, which owns their flags
    return nullptr;
}

AstNode* Lowering::lowerCase(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    AstNodeExpr* const selectorp = lowerExpr(*stmt.selector, nullptr);
    if (!selectorp) return nullptr;
    AstNodeDType* const selDtp = selectorp->dtypep();
    AstCaseItem* itemsp = nullptr;
    AstCaseItem* currentp = nullptr;  // The "others" alternative, if seen
    for (const auto& item : stmt.caseItems) {
        AstNode* const bodyp = lowerStmtList(item.second);
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(item.first, selDtp, cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            if (bodyp) bodyp->deleteTree();
            if (itemsp) itemsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        if (cops.empty() && currentp) {
            // A second "others" alternative can never be selected
            if (bodyp) bodyp->deleteTree();
            continue;
        }
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
    AstCase* const casep = new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
    // A case over an enumeration is exhaustive; unlisted bit patterns are illegal
    if (VN_CAST(selectorp->dtypep()->skipRefToEnump(), EnumDType)) casep->uniquePragma(true);
    return casep;
}

AstNode* Lowering::lowerStmtList(const std::vector<StmtP>& stmts) {
    AstNode* headp = nullptr;
    // Conditions that must hold for the following statements to run, set by exit/next
    AstNodeExpr* runCondp = nullptr;
    for (const StmtP& stmtp : stmts) {
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
            AstNode* const setp = setLoopFlag(stmtp->flp, flagp, condp);
            if (setp) headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, setp) : setp;
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
        AstNode* nodep = lowerStmt(*stmtp);
        if (!nodep) continue;
        // Statements after a conditional exit/next only run when it was not taken
        if (runCondp) nodep = new AstIf{stmtp->flp, runCondp->cloneTree(false), nodep, nullptr};
        if (AstNodeExpr* const guardp = loopGuard(stmtp->flp)) {
            nodep = new AstIf{stmtp->flp, guardp, nodep, nullptr};
        }
        headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
    }
    if (runCondp) runCondp->deleteTree();
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
    const std::string name = std::string{"vhdlloop_"} + std::to_string(++m_loopSeq) + "__" + what;
    AstVar* const varp
        = new AstVar{flp, VVarType::VAR, name, findBasic(flp, VBasicDTypeKwd::LOGIC)};
    m_modp->addStmtsp(varp);
    return varp;
}

//! `if (cond) flag = 1`, or `flag = 1` when there is no condition
AstNode* Lowering::setLoopFlag(FileLine* flp, AstVar* flagp, AstNodeExpr* condp) {
    AstNodeExpr* const lhsp = new AstVarRef{flp, flagp, VAccess::WRITE};
    AstNodeExpr* const rhsp = bitConst(flp, '1');
    AstAssign* const assignp = new AstAssign{flp, lhsp, rhsp};
    if (!condp) return assignp;
    return new AstIf{flp, condp, assignp, nullptr};
}

//! Clear a loop flag before it is tested again
AstNode* Lowering::clearLoopFlag(FileLine* flp, AstVar* flagp) {
    AstNodeExpr* const lhsp = new AstVarRef{flp, flagp, VAccess::WRITE};
    AstNodeExpr* const rhsp = bitConst(flp, '0');
    return new AstAssign{flp, lhsp, rhsp};
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

AstNode* Lowering::lowerFor(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    int64_t left = 0;
    int64_t right = 0;
    bool up = stmt.rangeAscending;
    if (!stmt.c) {
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
        left = typep->left;
        right = typep->right;
        up = typep->ascending;
    } else if (!staticInt(stmt.b, left) || !staticInt(stmt.c, right)) {
        unsupportedError(flp, "loop range that is not static");
        return nullptr;
    }
    if (up ? right < left : right > left) {
        // An empty range: the loop body never runs
        return nullptr;
    }
    if (std::llabs(right - left) > 4096) {
        unsupportedError(flp, "loop range with more than 4096 iterations");
        return nullptr;
    }
    LoopFrame frame;
    frame.label = stmt.loopLabel;
    frame.exitp = newLoopFlag(flp, "exit");
    frame.nextp = newLoopFlag(flp, "next");
    frame.guard = usesExitNext(stmt.body);
    m_loops.push_back(frame);
    AstNode* headp = clearLoopFlag(flp, frame.exitp);
    AstNode* const initp = clearLoopFlag(flp, frame.nextp);
    headp = AstNode::addNext<AstNode, AstNode>(headp, initp);

    // The loop variable is a static constant, so each iteration can be unrolled
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
            AstNode* const clearNextp = clearLoopFlag(flp, frame.nextp);
            headp = AstNode::addNext<AstNode, AstNode>(headp, clearNextp);
        }
        AstNode* const bodyp = lowerStmtList(stmt.body);
        if (bodyp) {
            AstNode* const guardp = new AstIf{flp, exitNotp->cloneTree(false), bodyp, nullptr};
            headp = AstNode::addNext<AstNode, AstNode>(headp, guardp);
        }
        if (value == right) break;
    }
    exitNotp->deleteTree();
    if (hadConst) {
        m_consts[stmt.loopVar] = savedValue;
    } else {
        m_consts.erase(stmt.loopVar);
    }
    m_loops.pop_back();
    return headp;
}

AstNode* Lowering::lowerWhile(const Stmt& stmt) {
    unsupportedError(stmt.flp, stmt.kind == Stmt::WHILE ? "while loop" : "bare loop");
    return nullptr;
}

void Lowering::lowerProcess(const Process& process) {
    if (!process.hasSensitivity) {
        unsupportedError(process.flp, "process without a sensitivity list (wait-based processes)");
        return;
    }
    // The process declarative part declares variables that hide module-level names
    const std::map<std::string, VarInfo> savedScope = m_scope;
    const std::map<std::string, int64_t> savedConsts = m_consts;
    const std::map<std::string, VType> savedTypes = m_types;
    const std::map<std::string, AstConst*> savedConstValues = m_constValues;
    const std::map<std::string, VType> savedConstTypes = m_constTypes;
    const std::map<std::string, AstConst*> savedEnumConsts = m_enumConsts;
    int varSeq = 0;
    for (const TypeDecl& typeDecl : process.types) registerType(typeDecl);
    for (const Decl& decl : process.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            // Mangle so the same variable name in two processes stays distinct
            const std::string mangled = "vhdlvar_" + std::to_string(++varSeq) + "__" + name.second;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (!decl.init) continue;
            AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
            AstNodeExpr* rhsp = lowerExpr(*decl.init, &type);
            if (!rhsp) {
                lhsp->deleteTree();
                continue;
            }
            rhsp = resizeExpr(rhsp, type, decl.flp);
            m_modp->addStmtsp(new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
        }
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
    m_scope = savedScope;
    m_consts = savedConsts;
    m_types = savedTypes;
    m_constValues = savedConstValues;
    m_constTypes = savedConstTypes;
    m_enumConsts = savedEnumConsts;
}

//! Register a declared name; a local declaration hides a name from a used package
bool Lowering::reserveName(const std::string& folded, const std::string& spelling, FileLine* flp) {
    if (m_imported.erase(folded)) {  // The local declaration shadows the package member
        m_consts.erase(folded);
        m_constValues.erase(folded);
        m_constTypes.erase(folded);
        m_enumConsts.erase(folded);
        m_types.erase(folded);
        return true;
    }
    if (m_scope.count(folded) || m_consts.count(folded) || m_constValues.count(folded)
        || m_enumConsts.count(folded) || m_types.count(folded)) {
        flp->v3error("VHDL: duplicate declaration of '" + spelling + "'");
        return false;
    }
    return true;
}

void Lowering::emitConstant(const Decl& decl, bool deferredOk) {
    if (!decl.init) {
        // A package body completes the constants its specification deferred
        if (!deferredOk) decl.flp->v3error("VHDL: constant without a value");
        return;
    }
    VType constType;
    bool intConstant = true;
    if (decl.type.flp && resolveType(decl.type, constType)) intConstant = constType.isInt();
    if (intConstant) {
        int64_t value = 0;
        if (!evalInt(*decl.init, value)) {
            unsupportedError(decl.flp, "constant value that is not a static integer expression");
            return;
        }
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            m_consts[name.first] = value;
        }
        return;
    }
    AstNodeExpr* valuep = lowerExpr(*decl.init, &constType);
    if (!valuep) return;
    if (AstConst* const foldedp = foldToConst(valuep)) valuep = foldedp;
    AstConst* const constp = VN_CAST(valuep, Const);
    if (!constp) {
        unsupportedError(decl.flp, "constant value that is not static");
        valuep->deleteTree();
        return;
    }
    constp->dtypep(constType.dtp);
    for (size_t i = 0; i < decl.names.size(); ++i) {
        const auto& name = decl.names[i];
        if (!reserveName(name.first, name.second, decl.flp)) continue;
        const bool last = i + 1 == decl.names.size();
        m_ownedConsts.push_back(
            std::unique_ptr<AstConst>(last ? constp : constp->cloneTree(false)));
        m_constValues[name.first] = m_ownedConsts.back().get();
        m_constTypes[name.first] = constType;
    }
}

void Lowering::importPackage(const std::string& name) {
    if (!m_packagesp) return;
    const auto pkgIt = m_packagesp->packages.find(name);
    if (pkgIt == m_packagesp->packages.end()) return;  // Standard package: intrinsics
    const PackageInfo& info = pkgIt->second;
    for (const auto& type : info.types) {
        if (m_types.count(type.first)) continue;
        m_types[type.first] = type.second;
        m_imported.insert(type.first);
    }
    for (const auto& constant : info.consts) {
        if (m_consts.count(constant.first) || m_constValues.count(constant.first)) continue;
        m_consts[constant.first] = constant.second;
        m_imported.insert(constant.first);
    }
    for (const auto& value : info.constValues) {
        if (m_consts.count(value.first) || m_constValues.count(value.first)) continue;
        m_ownedConsts.push_back(std::unique_ptr<AstConst>(value.second->cloneTree(false)));
        m_constValues[value.first] = m_ownedConsts.back().get();
        m_imported.insert(value.first);
    }
    for (const auto& type : info.constTypes) {
        if (m_constTypes.count(type.first)) continue;
        m_constTypes[type.first] = type.second;
    }
    for (const auto& literal : info.enumConsts) {
        if (m_enumConsts.count(literal.first)) continue;
        m_ownedConsts.push_back(std::unique_ptr<AstConst>(literal.second->cloneTree(false)));
        m_enumConsts[literal.first] = m_ownedConsts.back().get();
        m_imported.insert(literal.first);
    }
}

void Lowering::emitPackage(const PackageUnit& pkg, PackageTable::Impl& table) {
    // A package sees only its own declarations
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ownedConsts.clear();
    m_imported.clear();
    m_scope.clear();
    for (const TypeDecl& typeDecl : pkg.types) registerType(typeDecl);
    for (const Decl& decl : pkg.decls) emitConstant(decl, !pkg.isBody);

    PackageInfo info;
    info.types = std::move(m_types);
    info.consts = std::move(m_consts);
    info.constValues = std::move(m_constValues);
    info.constTypes = std::move(m_constTypes);
    info.enumConsts = std::move(m_enumConsts);
    info.owned = std::move(m_ownedConsts);
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ownedConsts.clear();

    const auto existing = table.packages.find(pkg.name);
    if (existing == table.packages.end()) {
        table.packages.emplace(pkg.name, std::move(info));
        return;
    }
    // A package body (or a repeated declaration) adds to what is already published
    PackageInfo& dest = existing->second;
    for (const auto& type : info.types) dest.types[type.first] = type.second;
    for (const auto& constant : info.consts) dest.consts[constant.first] = constant.second;
    for (const auto& value : info.constValues) dest.constValues[value.first] = value.second;
    for (const auto& type : info.constTypes) dest.constTypes[type.first] = type.second;
    for (const auto& literal : info.enumConsts) dest.enumConsts[literal.first] = literal.second;
    for (std::unique_ptr<AstConst>& constp : info.owned) dest.owned.push_back(std::move(constp));
}

void Lowering::emit(const EntityUnit& entity, const Architecture& arch) {
    m_modp = new AstModule{entity.flp, entity.spelling, entity.libname};
    m_modp->inLibrary(entity.inLibrary || arch.inLibrary);

    for (const auto& use : entity.uses) importPackage(use.second);
    for (const auto& use : arch.uses) importPackage(use.second);

    // Types are visible to the whole declarative part, and must be registered first
    for (const TypeDecl& typeDecl : arch.types) registerType(typeDecl);

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
            emitConstant(decl, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
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
        const VType ltype = typeOfTarget(*stmtp->target, lhsp);
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
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ownedConsts.clear();
    m_types.clear();
}

//######################################################################
// Entry point

void lowerPackages(const std::vector<PackageUnit>& packages, AstNetlist* rootp,
                   PackageTable& table) {
    Lowering lowering{rootp};
    // Specifications first: a body completes the constants its spec declared
    for (const PackageUnit& pkg : packages) {
        if (!pkg.isBody) lowering.emitPackage(pkg, table.impl());
    }
    for (const PackageUnit& pkg : packages) {
        if (pkg.isBody) lowering.emitPackage(pkg, table.impl());
    }
}

void lower(const EntityUnit& entity, const Architecture& arch, AstNetlist* rootp,
           const PackageTable& table) {
    Lowering lowering{rootp, &table.impl()};
    lowering.emit(entity, arch);
}

}  // namespace vhdlnative
