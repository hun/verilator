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

#include "V3Const.h"
#include "V3Error.h"
#include "V3FileLine.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
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
    //! Bit offset of a declared array index from the least significant element;
    //! the right bound is the least significant element in either direction
    int indexOffset(int index) const { return ascending ? right - index : index - right; }
};

//! Elaborated contents of one package, publishable to the units that use it
//! An alias: the designator it renames, and the type its mark declares
struct AliasInfo final {
    const Expr* designatorp = nullptr;
    VType type;
    bool hasType = false;
};

struct PackageInfo final {
    std::map<std::string, AliasInfo> aliases;
    std::map<std::string, VType> types;
    std::map<std::string, int64_t> consts;
    std::map<std::string, AstConst*> constValues;
    std::map<std::string, VType> constTypes;
    std::map<std::string, AstConst*> enumConsts;
    std::vector<const SubprogramDecl*> subprograms;  // Prototypes and bodies, by declaration
    std::vector<std::unique_ptr<AstConst>> owned;  // Owns the constants above
};

struct PackageTable::Impl final {
    std::map<std::string, PackageInfo> packages;  // Folded package name -> contents
};

PackageTable::PackageTable()
    : m_implp{new Impl} {}
PackageTable::~PackageTable() = default;

//! Statically bound value of one generic actual
struct GenericBinding final {
    bool isInt = false;
    int64_t intValue = 0;
    AstConst* valuep = nullptr;  // Typed constant; cloned when registered, else owned here
};
using GenericBindings = std::map<std::string, GenericBinding>;
class Lowering final {
    AstNetlist* const m_rootp;  // Netlist to emit into
    AstModule* m_modp = nullptr;  // Module being emitted
    struct VarInfo final {
        AstVar* varp = nullptr;
        VType type;
    };
    //! A visible subprogram: its declaration and whether a process declared it
    struct SubprogramRef final {
        const SubprogramDecl* declp = nullptr;
        bool processLocal = false;
        int frame = -1;  // Active inline frame that owns it, or -1
    };
    //! Module-level declarations a process hides, restored when it ends
    struct ScopeSave final {
        std::map<std::string, VarInfo> scope;
        std::map<std::string, int64_t> consts;
        std::map<std::string, VType> types;
        std::map<std::string, AstConst*> constValues;
        std::map<std::string, VType> constTypes;
        std::map<std::string, AstConst*> enumConsts;
        std::map<std::string, std::vector<SubprogramRef>> subprograms;
    };
    //! One name bound during subprogram inlining: an expression value, or an lvalue
    struct InlineBinding final {
        AstNodeExpr* exprp = nullptr;  // Bound expression; cloned at each use
        bool lvalue = false;  // exprp denotes a writable location (an out/inout parameter)
        bool readable = true;  // False for an out signal parameter, whose reads are rejected
        VType type;  // Resolved type of the bound value, for attributes of the name
    };
    using InlineEnv = std::map<std::string, InlineBinding>;  // Bindings while inlining
    using InlineCont = std::function<AstNodeExpr*(InlineEnv&)>;  // Continuation for inlining
    //! Continuations of active static function loops, for labeled exit/next
    struct FuncLoopTarget final {
        std::string label;
        const InlineCont* nextp = nullptr;  // Continue with the following iteration
        const InlineCont* exitp = nullptr;  // Continue after the loop
    };
    //! An active inlining: the body, the scope its callee captures, and its bindings
    struct InlineFrame final {
        const SubprogramDecl* declp = nullptr;
        ScopeSave entryScope;  // The caller's scope when the call was made
        ScopeSave liveScope;  // Grows with procedure locals
        InlineEnv* envp = nullptr;
    };
    //! Active bindings, referenced before the scope tables: a callee owns its map while
    //! its body lowers, so nested lowering sees it
    InlineEnv m_ownEnv;  // Bindings outside any inlining: always empty
    InlineEnv* m_inlineP = &m_ownEnv;
    InlineEnv& activeEnv() { return *m_inlineP; }
    const InlineEnv& activeEnv() const { return *m_inlineP; }
    std::deque<InlineFrame> m_inlineFrames;
    std::vector<FuncLoopTarget> m_funcLoopTargets;
    std::set<const SubprogramDecl*> m_inlineBodies;  // Recursion guard
    std::map<std::string, std::vector<SubprogramRef>> m_subprograms;  // Visible, by name
    std::vector<const Stmt*> m_procTail;  // Statements after the current procedure return
    int m_concurrentProc = 0;  // Lowering a concurrent procedure call body
    ScopeSave m_moduleScope;  // Module declarative part, hidden by processes
    int m_procVarSeq = 0;  // Disambiguates procedure local variables
    const VType* m_funcRetp = nullptr;  // Return-type context for function bodies
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
    int m_genSeq = 0;  // Disambiguates sibling generate bodies
    std::string m_genSuffix;  // Suffix for names created inside a generate
    const ModuleMap* m_modulesp = nullptr;  // Modules of the instantiated entities
    PackageTable::Impl* m_publishp = nullptr;  // Package table to publish into
    const PackageTable::Impl* m_packagesp = nullptr;  // Packages visible to this unit
    std::set<std::string> m_imported;  // Names that came from a used package
    std::set<std::string> m_variables;  // Module-level variables, which are not signals
    std::map<std::string, AliasInfo> m_aliases;
    mutable std::set<std::string> m_aliasBusy;  // Aliases being expanded (recursion guard)
    Design* const m_designp;  // Design context for generic specialization

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
    //! Element type of an array or vector; a vector's element is a scalar of its kind
    VType elementType(const VType& type, FileLine* flp) const {
        if (type.kind == VType::ARRAY && type.elemp) return *type.elemp;
        VType elem;
        elem.kind = type.kwd == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
        elem.dtp = findBasic(flp, type.kwd);
        return elem;
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
    void emitConstant(const std::vector<std::pair<std::string, std::string>>& names,
                      const TypeSpec& type, const Expr* initp, FileLine* flp, bool deferredOk);
    bool registerGenerics(const EntityUnit& entity, const GenericBindings* bindings, bool probe);
    AstModule* specializeInstance(const Concurrent& conc, const EntityUnit& entity,
                                  AstModule* basep);
    bool reserveName(const std::string& folded, const std::string& spelling, FileLine* flp);
    AstNodeDType* enumDType(FileLine* flp, const TypeDecl& decl, int width);
    bool applyRange(const TypeSpec& spec, VType& type);
    bool constraintBounds(const TypeSpec& spec, int64_t& left, int64_t& right, bool& ascending);
    bool evalInt(const Expr& expr, int64_t& value) const;
    bool staticInt(const ExprP& exprp, int64_t& value) const {
        return exprp && evalInt(*exprp, value);
    }

    AstVar* lookupVar(const Expr& nameExpr) const {
        const auto it = m_scope.find(nameExpr.text);
        return it == m_scope.end() ? nullptr : it->second.varp;
    }
    bool isIntrinsic(const std::string& name) const {
        static const char* const names[] = {"is_x",
                                            "is_x01",
                                            "is_x01z",
                                            "is_ux01",
                                            "resize",
                                            "shift_left",
                                            "shift_right",
                                            "rotate_left",
                                            "rotate_right",
                                            "minimum",
                                            "maximum",
                                            "conv_integer",
                                            "conv_std_logic_vector",
                                            "conv_unsigned",
                                            "conv_signed",
                                            "to_stdlogicvector",
                                            "to_stdulogicvector",
                                            "to_bitvector",
                                            "to_stdulogic",
                                            "to_bit"};
        for (const char* const candidate : names) {
            if (name == candidate) return true;
        }
        return false;
    }
    AstNodeExpr* lowerIntrinsic(const Expr& expr);
    AstNodeExpr* lowerEdgeCall(const Expr& expr);
    AstNodeExpr* makeRotate(FileLine* flp, AstNodeExpr* valuep, AstNodeExpr* amountp, bool left);
    AstNodeExpr* makeMod(FileLine* flp, AstNodeExpr* lp, AstNodeExpr* rp, AstNodeDType* dtp,
                         bool sign);
    AstNodeExpr* makeRem(FileLine* flp, AstNodeExpr* lp, AstNodeExpr* rp, AstNodeDType* dtp,
                         bool sign);
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
    //! Build an AstCase; a case over an enumeration is exhaustive
    AstCase* makeCase(FileLine* flp, AstNodeExpr* selectorp, AstCaseItem* itemsp) const {
        AstCase* const casep = new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
        if (VN_CAST(selectorp->dtypep()->skipRefToEnump(), EnumDType)) casep->uniquePragma(true);
        return casep;
    }
    void lowerWaitProcess(const Process& process);
    ScopeSave saveScope() const;
    void restoreScope(const ScopeSave& saved);
    void emitProcessDecls(const Process& process, std::set<std::string>& varNames);
    AstNode* lowerFor(const Stmt& stmt);
    static void collectReadNames(const std::vector<StmtP>& stmts, std::set<std::string>& names);
    bool stmtsContainEdge(const std::vector<StmtP>& stmts) const;
    static void collectExprNames(const Expr& expr, std::set<std::string>& names);
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
    bool packageNameOf(const Expr& prefix, std::string& name) const;
    AstNodeExpr* lowerAlias(const Expr& nameExpr, bool write);
    void registerAlias(const AliasDecl& alias);
    AstNodeExpr* lowerPackageMember(const Expr& expr, const std::string& pkgName);
    VType typeOfTarget(const Expr& target, AstNodeExpr* lhsp) const;
    AstNodeExpr* lowerAlternatives(const Concurrent& conc, const VType& type);
    AstNode* lowerClockedAlternatives(const Concurrent& conc, const VType& type,
                                      AstNodeExpr* lhsp);
    bool concHasEdge(const Concurrent& conc) const;
    AstNode* lowerSelected(const Concurrent& conc);
    void emitConcurrent(const Concurrent& conc);
    void emitLocalDecls(const std::vector<Decl>& decls, const std::vector<TypeDecl>& types,
                        const char* prefix);
    bool lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                           std::vector<AstConst*>& cops);
    AstNode* lowerStmt(const Stmt& stmt);
    AstNode* lowerStmtList(const std::vector<StmtP>& stmts);
    AstNode* lowerStmtRange(const std::vector<StmtP>& stmts, std::size_t begin, std::size_t end);
    AstNode* lowerIf(const Stmt& stmt);
    AstNode* lowerAssign(const Stmt& stmt, bool concurrent, bool variable = false);
    void lowerProcess(const Process& process);
    void registerSubprograms(const std::vector<SubprogramDecl>& decls, int frame,
                             bool processLocal = false);
    void setScope(const ScopeSave& saved);
    bool subprogramMatches(const Expr& call, const SubprogramDecl& sub, std::string& why) const;
    std::vector<const SubprogramDecl*> visibleSubprograms(const std::string& name) const;
    const SubprogramDecl* resolveCall(const Expr& call, bool function,
                                      const SubprogramRef** foundpp = nullptr);
    void captureBaseFor(const SubprogramRef& ref, ScopeSave& base, InlineEnv& env) const;
    AstNodeExpr* inlineBinding(FileLine* flp, const std::string& name, bool write) const;
    bool bindSubprogramFormals(const Expr& call, const SubprogramDecl& sub, InlineEnv& env,
                               AstNode*& prologsp, bool lvalueFormals, bool concurrent);
    AstNodeExpr* lowerFunctionCall(const Expr& call, const VType* context);
    AstNodeExpr* evalFuncBlock(const std::vector<StmtP>& stmts, std::size_t index, InlineEnv& env,
                               const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncBlockBody(const std::vector<StmtP>& stmts, std::size_t index,
                                   const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncBranches(const Stmt& stmt, std::size_t branchIndex,
                                  const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncCase(const Stmt& stmt, const InlineCont& cont,
                              const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncFor(const Stmt& stmt, const InlineCont& postCont, int depth);
    AstNodeExpr* updateLocalElement(FileLine* flp, AstNodeExpr* oldp, AstNodeExpr* valuep,
                                    AstNodeDType* elemDtp, AstNodeExpr* lsbp, int width);
    AstNode* lowerProcedureCall(const Expr& callExpr, bool concurrent);
    AstNode* lowerProcChain(const std::vector<const Stmt*>& stmts, std::size_t index,
                            bool& fallsThroughp);
    AstNode* lowerProcChainWithTail(const std::vector<StmtP>& stmts,
                                    const std::vector<const Stmt*>& tail, bool& fallsThroughp);
    AstNode* lowerProcIf(const Stmt& stmt, std::size_t branchIndex,
                         const std::vector<const Stmt*>& tail, bool& fallsThroughp);
    AstNode* lowerProcCase(const Stmt& stmt, const std::vector<const Stmt*>& tail,
                           bool& fallsThroughp);
    static void deleteInlineEnv(InlineEnv& env);
    static InlineEnv cloneInlineEnv(const InlineEnv& env);
    static std::vector<const Stmt*> rawPtrs(const std::vector<StmtP>& stmts);

public:
    explicit Lowering(AstNetlist* rootp, const PackageTable::Impl* packagesp = nullptr,
                      const ModuleMap* modulesp = nullptr, Design* designp = nullptr)
        : m_rootp{rootp}
        , m_packagesp{packagesp}
        , m_modulesp{modulesp}
        , m_designp{designp} {}
    ~Lowering() = default;

    bool emit(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
              const GenericBindings* bindings = nullptr);
    AstNode* lowerInstance(const Concurrent& conc);
    void emitPackage(const PackageUnit& pkg, PackageTable::Impl& table);
};

//######################################################################
// Lowering

bool Lowering::evalInt(const Expr& expr, int64_t& value) const {
    switch (expr.kind) {
    case Expr::INT_LIT: value = expr.intValue; return true;
    case Expr::NAME: {
        const auto bindIt = activeEnv().find(expr.text);
        if (bindIt != activeEnv().end() && VN_IS(bindIt->second.exprp, Const)) {
            value = static_cast<int64_t>(VN_AS(bindIt->second.exprp, Const)->num().toSInt());
            return true;
        }
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
        bool ascending = false;
        if (!constraintBounds(decl.index, left, right, ascending)) {
            // Unconstrained array type: every use must supply the index constraint
            type.left = 0;
            type.right = 0;
            type.width = 0;
            break;
        }
        type.left = static_cast<int>(left);
        type.right = static_cast<int>(right);
        type.ascending = ascending;
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

//! Bounds of an index constraint; a discrete range attribute such as `sig'range`
//! contributes the bounds and direction of its prefix
bool Lowering::constraintBounds(const TypeSpec& spec, int64_t& left, int64_t& right,
                                bool& ascending) {
    if (spec.right) {
        if (!staticInt(spec.left, left) || !staticInt(spec.right, right)) return false;
        ascending = spec.ascending;
        return true;
    }
    if (!spec.left || spec.left->kind != Expr::ATTR
        || (spec.left->text != "range" && spec.left->text != "reverse_range") || !spec.left->a
        || spec.left->a->kind != Expr::NAME) {
        return false;
    }
    VType prefix;
    if (!typeOfDesignator(*spec.left->a, prefix)) return false;
    left = prefix.left;
    right = prefix.right;
    ascending = prefix.ascending;
    if (spec.left->text == "reverse_range") {
        // The same bounds in the opposite direction
        left = prefix.right;
        right = prefix.left;
        ascending = !prefix.ascending;
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
    bool ascending = false;
    if (!constraintBounds(spec, left, right, ascending)) {
        unsupportedError(flp, "non-static array range");
        return false;
    }
    const VBasicDTypeKwd kwd = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
    const VSigning signing = mark == "signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
    out = vectorType(flp, kwd, signing,
                     std::abs(static_cast<int>(left) - static_cast<int>(right)) + 1);
    out.left = static_cast<int>(left);
    out.right = static_cast<int>(right);
    out.ascending = ascending;
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
        if (activeEnv().count(expr.text)) return inlineBinding(flp, expr.text, false);
        const auto constIt = m_consts.find(expr.text);
        if (constIt != m_consts.end()) return makeIntConst(flp, constIt->second);
        const auto valueIt = m_constValues.find(expr.text);
        if (valueIt != m_constValues.end()) return valueIt->second->cloneTree(false);
        const auto enumIt = m_enumConsts.find(expr.text);
        if (enumIt != m_enumConsts.end()) return enumIt->second->cloneTree(false);
        const auto it = m_scope.find(expr.text);
        if (it == m_scope.end()) {
            if (AstNodeExpr* const aliasp = lowerAlias(expr, false)) return aliasp;
            if (m_subprograms.count(expr.text)) {
                // A function that has no formal arguments may be called without parentheses
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
            unsupportedError(flp, "aggregate without a known target type");
            return nullptr;
        }
        return lowerAggregate(expr, *context);
    }
    case Expr::QUAL: {
        // A type qualification fixes the operand's type and keeps its width
        AstNodeExpr* valuep = lowerExpr(*expr.a, context);
        if (!valuep) return nullptr;
        const VType valueType = typeOfDType(valuep->dtypep());
        const auto declared = m_types.find(expr.text);
        if (declared != m_types.end()) return resizeExpr(valuep, declared->second, flp);
        VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
        VSigning signing = VSigning::UNSIGNED;
        if (expr.text == "bit_vector" || expr.text == "bit") kwd = VBasicDTypeKwd::BIT;
        if (expr.text == "signed") signing = VSigning::SIGNED;
        const VType target = vectorType(flp, kwd, signing, valueType.width);
        return resizeExpr(valuep, target, flp);
    }
    case Expr::ATTR: return lowerAttr(expr);
    case Expr::SELECTED: return lowerSelectedExpr(expr);
    case Expr::PAREN: return lowerExpr(*expr.a, context);
    case Expr::UNARY: {
        AstNodeExpr* operandp = lowerExpr(*expr.a, nullptr);
        if (!operandp) return nullptr;
        const VType type = typeOfDType(operandp->dtypep());
        if (expr.text == "abs") {
            if (!type.isInt()) {
                unsupportedError(flp, "abs of a non-integer value");
                operandp->deleteTree();
                return nullptr;
            }
            // |x| is x when x is not negative, and -x otherwise
            AstNodeExpr* const zerop = makeIntConst(flp, 0);
            AstNodeExpr* const ltZeroP = new AstLtS{flp, operandp->cloneTree(false), zerop};
            ltZeroP->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
            AstNegate* const negp = new AstNegate{flp, operandp->cloneTree(false)};
            negp->dtypep(type.dtp);
            AstCond* const resultp = new AstCond{flp, ltZeroP, negp, operandp};
            resultp->dtypep(type.dtp);
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
        if (expr.a && expr.a->kind == Expr::NAME) {
            if (expr.a->text == "rising_edge" || expr.a->text == "falling_edge") {
                return lowerEdgeCall(expr);
            }
            if (isIntrinsic(expr.a->text)) return lowerIntrinsic(expr);
            if (isConversion(expr.a->text)) return lowerConversion(expr);
            // A declared type mark applied to a value is a type conversion
            const auto typeIt = m_types.find(expr.a->text);
            if (typeIt != m_types.end()) {
                if (expr.args.size() != 1) {
                    unsupportedError(flp, "type conversion with "
                                              + std::to_string(expr.args.size()) + " arguments");
                    return nullptr;
                }
                AstNodeExpr* valuep = lowerExpr(*expr.args[0], &typeIt->second);
                if (!valuep) return nullptr;
                return resizeExpr(valuep, typeIt->second, flp);
            }
        }
        if (expr.a && expr.a->kind == Expr::NAME && m_subprograms.count(expr.a->text)) {
            return lowerFunctionCall(expr, context);
        }
        return lowerDesignator(expr, false);
    case Expr::CALL: return lowerFunctionCall(expr, context);
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
        VType elemType = elementType(*context, flp);
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

    if (op == "sll" || op == "srl" || op == "sla" || op == "sra" || op == "rol" || op == "ror") {
        if (!lt.isVector()) {
            unsupportedError(flp, "shift or rotate of a non-vector value");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        AstNodeDType* const dtp = lp->dtypep();
        const int width = dtp->width();
        if (op == "rol" || op == "ror") return makeRotate(flp, lp, rp, op == "rol");
        AstNodeExpr* resultp = nullptr;
        if (op == "sll" || op == "sla") {
            resultp = new AstShiftL{flp, lp, rp, width};
        } else if (op == "sra") {
            // Arithmetic only for a signed operand; an unsigned value shifts in zeros
            resultp = lt.isSigned() ? static_cast<AstNodeExpr*>(new AstShiftRS{flp, lp, rp, width})
                                    : static_cast<AstNodeExpr*>(new AstShiftR{flp, lp, rp, width});
        } else {
            resultp = new AstShiftR{flp, lp, rp, width};
        }
        resultp->dtypep(dtp);
        return resultp;
    }
    if (op == "**") {
        // A static exponent unrolls into a chain of multiplications
        int64_t exponent = 0;
        if (!staticInt(expr.b, exponent) || exponent < 0 || exponent > 64) {
            unsupportedError(flp, "non-static exponent");
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
        AstNodeExpr* resultp = nullptr;
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
    Edge edge;
    edge.varp = varp;
    edge.type = expr.a->text == "rising_edge" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
    m_edges.push_back(edge);
    // The sensitivity item supplies the edge; at time zero no signal has had an event
    return bitConst(flp, m_initial ? '0' : '1');
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

//! Standard-package functions that lower to an AST idiom, keyed by folded name
AstNodeExpr* Lowering::lowerIntrinsic(const Expr& expr) {
    FileLine* const flp = expr.flp;
    const std::string& fn = expr.a->text;
    const std::vector<ExprP>& argsp = expr.args;
    // The model is 2-state, so no value can be a metavalue
    if (fn == "is_x" || fn == "is_x01" || fn == "is_x01z" || fn == "is_ux01") {
        return bitConst(flp, '0');
    }
    if (argsp.empty()) {
        unsupportedError(flp, "function '" + fn + "' without an argument");
        return nullptr;
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
            signing = fn == "conv_signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
        }
        const VType target = vectorType(flp, kwd, signing, static_cast<int>(width));
        return resizeExpr(valuep, target, flp);
    }
    if (fn == "shift_left" || fn == "shift_right") {
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
        // shift_right sign-extends a signed operand; shift_left does not
        if (fn == "shift_left") {
            resultp = new AstShiftL{flp, valuep, amountp, dtp->width()};
        } else if (dtp->isSigned()) {
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
                         VSigning::UNSIGNED, valueType.width);
        return resizeExpr(valuep, target, flp);
    }
    if (fn == "to_stdlogicvector" || fn == "to_stdulogicvector" || fn == "to_bitvector") {
        AstNodeExpr* valuep = lowerExpr(*argsp[0], nullptr);
        if (!valuep) return nullptr;
        const VType valueType = typeOfDType(valuep->dtypep());
        const VType target
            = vectorType(flp, fn == "to_bitvector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC,
                         VSigning::UNSIGNED, valueType.width);
        return resizeExpr(valuep, target, flp);
    }
    unsupportedError(flp, "function '" + fn + "'");
    return nullptr;
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
        if (activeEnv().count(expr.text)) return inlineBinding(flp, expr.text, write);
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
        // The right bound is the least significant element in either direction
        AstNodeExpr* offsetp = indexp;
        if (baseType.ascending) {
            offsetp = new AstSub{flp, makeIntConst(flp, baseType.right), indexp};
        } else if (baseType.right != 0) {
            offsetp = new AstSub{flp, indexp, makeIntConst(flp, baseType.right)};
        }
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
            return bitConst(flp, m_initial ? '0' : '1');
        }
    }
    if (expr.kind == Expr::INDEX && expr.a && expr.a->kind == Expr::NAME
        && (expr.a->text == "rising_edge" || expr.a->text == "falling_edge")) {
        return lowerEdgeCall(expr);
    }
    if (containsEdgeCall(expr)) {
        // A compound wait condition may test a level and one clock edge together
        AstNodeExpr* const compoundp = lowerExpr(expr, nullptr);
        if (!compoundp) return nullptr;
        if (compoundp->width() != 1) {
            flp->v3error("VHDL: condition is not a 1-bit value");
            compoundp->deleteTree();
            return nullptr;
        }
        return compoundp;
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
    const auto bindIt = activeEnv().find(expr.text);
    if (bindIt != activeEnv().end()) {
        if (!bindIt->second.type.dtp) return nullptr;
        return &bindIt->second.type;
    }
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

//! An alias takes the index bounds its mark declares; the bits come from the designator
void Lowering::registerAlias(const AliasDecl& alias) {
    if (!alias.designator) return;
    AliasInfo info;
    info.designatorp = alias.designator.get();
    if (alias.mark.flp && resolveType(alias.mark, info.type)) info.hasType = true;
    m_aliases[alias.name] = info;
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
                    if (!staticInt(choicep->b, left) || !staticInt(choicep->c, right)) {
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
        AstNodeExpr* valuep = nullptr;
        if (type.isVector() && segment.width > type.elemWidth) {
            // A range association's value is either one element, repeated over the
            // whole range, or spans the range itself
            const VType elemType = elementType(type, flp);
            if (elemType.width <= 0) {
                if (resultp) resultp->deleteTree();
                return nullptr;
            }
            valuep = lowerExpr(*segment.exprp, &elemType);
            if (!valuep) {
                if (resultp) resultp->deleteTree();
                return nullptr;
            }
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
            if (!valuep) {
                if (resultp) resultp->deleteTree();
                return nullptr;
            }
            valuep = resizeExpr(valuep, segType, flp);
        }
        if (!VN_IS(valuep, Const)) allConst = false;
        resultp
            = resultp ? static_cast<AstNodeExpr*>(new AstConcat{flp, valuep, resultp}) : valuep;
    }
    if (allConst) {
        // Fold an aggregate of constants down to a single constant
        if (AstConst* const constp = foldToConst(resultp)) {
            constp->dtypep(type.dtp);
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
bool Lowering::concHasEdge(const Concurrent& conc) const {
    for (const auto& alt : conc.alternatives) {
        if (alt.second && containsEdgeCall(*alt.second)) return true;
    }
    return false;
}

//! A conditional assignment gated by a clock edge is a register: if/elsif with
//! non-blocking assignments under the edge sensitivity
AstNode* Lowering::lowerClockedAlternatives(const Concurrent& conc, const VType& type,
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
    AstNode* restp = nullptr;
    for (std::size_t i = conc.alternatives.size(); i-- > 0;) {
        const auto& alt = conc.alternatives[i];
        AstNodeExpr* valuep = lowerExpr(*alt.first, &type);
        if (!valuep) {
            if (restp) restp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, type, flp);
        const bool level = i < firstEdged;
        AstNode* const assignp
            = level ? static_cast<AstNode*>(new AstAssign{flp, lhsp->cloneTree(false), valuep})
                    : static_cast<AstNode*>(new AstAssignDly{flp, lhsp->cloneTree(false), valuep});
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
    return makeCase(flp, selectorp, itemsp);
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
    // A concurrent procedure call drives its signal actuals as continuous assignments
    if (m_concurrentProc) {
        return new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                             new AstAssignW{stmt.flp, lhsp, rhsp}};
    }
    // Only variable assignments update immediately; a signal write is nonblocking
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
    if (stmt.kind == Stmt::REPORT) return nullptr;  // Only its syntax matters
    if (stmt.kind == Stmt::CALL) return lowerProcedureCall(*stmt.value, m_concurrentProc > 0);
    if (stmt.kind == Stmt::RETURN) {
        unsupportedError(stmt.flp, "return outside a subprogram body");
        return nullptr;
    }
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
    return makeCase(flp, selectorp, itemsp);
}

AstNode* Lowering::lowerStmtList(const std::vector<StmtP>& stmts) {
    return lowerStmtRange(stmts, 0, stmts.size());
}

AstNode* Lowering::lowerStmtRange(const std::vector<StmtP>& stmts, std::size_t begin,
                                  std::size_t end) {
    AstNode* headp = nullptr;
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

void Lowering::collectReadNames(const std::vector<StmtP>& stmts, std::set<std::string>& names) {
    for (const StmtP& stmtp : stmts) {
        if (!stmtp) continue;
        if (stmtp->value) collectExprNames(*stmtp->value, names);
        if (stmtp->selector) collectExprNames(*stmtp->selector, names);
        if (stmtp->b) collectExprNames(*stmtp->b, names);
        if (stmtp->c) collectExprNames(*stmtp->c, names);
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
        collectReadNames(stmtp->body, names);
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
    const ScopeSave saved = saveScope();
    std::set<std::string> varNames;
    emitProcessDecls(process, varNames);
    // Find the first wait; statements around it run at time zero and per activation
    std::size_t waitIndex = process.body.size();
    const Stmt* waitp = nullptr;
    for (std::size_t i = 0; i < process.body.size(); ++i) {
        if (process.body[i] && process.body[i]->kind == Stmt::WAIT) {
            waitIndex = i;
            waitp = process.body[i].get();
            break;
        }
    }
    if (!waitp) {
        unsupportedError(flp, "process without a sensitivity list or wait statement");
        return;
    }
    // A trailing bare wait makes the process single-shot: it suspends forever after
    // the first activation
    std::size_t stopIndex = process.body.size();
    bool singleShot = false;
    for (std::size_t i = waitIndex + 1; i < process.body.size(); ++i) {
        if (!process.body[i] || process.body[i]->kind != Stmt::WAIT) continue;
        const Stmt* const laterp = process.body[i].get();
        const bool last = i + 1 == process.body.size();
        if (!last || !laterp->waitForever) {
            unsupportedError(flp, "process with more than one wait statement");
            return;
        }
        singleShot = true;
        stopIndex = i;
        break;
    }
    const std::vector<StmtP>& bodyStmts = process.body;
    // The statements before the wait run once at time zero
    m_initial = true;
    AstNode* const preInitp = lowerStmtRange(bodyStmts, 0, waitIndex);
    m_initial = false;
    if (preInitp) m_modp->addStmtsp(new AstInitialStatic{flp, preInitp});
    if (waitp->waitForever) {
        // A bare wait suspends the process forever
        restoreScope(saved);
        return;
    }
    if (!waitp->value) {
        unsupportedError(flp, "wait on a sensitivity list");
        return;
    }
    m_edges.clear();
    AstNodeExpr* const condp = lowerCond(*waitp->value);
    if (!condp) return;
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    // Sensitivity: the clock edges the condition names, plus the levels it tests
    AstSenItem* itemsp = nullptr;
    for (const Edge& edge : edges) {
        AstSenItem* const itemp
            = new AstSenItem{flp, edge.type, new AstVarRef{flp, edge.varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    std::set<std::string> levelNames;
    collectExprNames(*waitp->value, levelNames);
    for (const std::string& name : levelNames) {
        const auto it = m_scope.find(name);
        if (it == m_scope.end()) continue;
        if (std::any_of(edges.begin(), edges.end(),
                        [&](const Edge& edge) { return edge.varp == it->second.varp; })) {
            continue;  // Already an edge item
        }
        AstSenItem* const itemp = new AstSenItem{
            flp, VEdgeType::ET_CHANGED, new AstVarRef{flp, it->second.varp, VAccess::READ}};
        itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
    }
    if (!itemsp) {
        unsupportedError(flp, "wait statement without a sensitivity item");
        condp->deleteTree();
        return;
    }
    // Each activation runs the statements after the wait, then those before it
    m_initial = false;
    AstNode* const afterp = lowerStmtRange(bodyStmts, waitIndex + 1, stopIndex);
    AstNode* const preActp = singleShot ? nullptr : lowerStmtRange(bodyStmts, 0, waitIndex);
    AstNode* bodyp = afterp;
    if (preActp) { bodyp = bodyp ? AstNode::addNext<AstNode, AstNode>(bodyp, preActp) : preActp; }
    if (singleShot) {
        // Latch that the single activation has happened
        const std::string name = "vhdlloop_" + std::to_string(++m_loopSeq) + "__once";
        AstVar* const oncep
            = new AstVar{flp, VVarType::VAR, name, findBasic(flp, VBasicDTypeKwd::LOGIC)};
        m_modp->addStmtsp(oncep);
        AstNodeExpr* const onceReadp = new AstVarRef{flp, oncep, VAccess::READ};
        AstNodeExpr* const notOncep = new AstNot{flp, onceReadp};
        notOncep->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        AstNode* innerp = nullptr;
        if (bodyp) {
            AstNode* const setp = new AstAssign{flp, new AstVarRef{flp, oncep, VAccess::WRITE},
                                                bitConst(flp, '1')};
            innerp = AstNode::addNext<AstNode, AstNode>(setp, bodyp);
        } else {
            innerp = new AstAssign{flp, new AstVarRef{flp, oncep, VAccess::WRITE},
                                   bitConst(flp, '1')};
        }
        AstNode* const gatedp = new AstIf{flp, condp, innerp, nullptr};
        bodyp = new AstIf{flp, notOncep, gatedp, nullptr};
    } else if (bodyp) {
        bodyp = new AstIf{flp, condp, bodyp, nullptr};
    } else {
        condp->deleteTree();
    }
    if (bodyp) {
        m_modp->addStmtsp(
            new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemsp}, bodyp});
    } else {
        itemsp->deleteTree();
    }
    restoreScope(saved);
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
    // An inline binding of the loop variable's name, e.g. a procedure formal, must not
    // shadow the iteration constant
    const auto boundIt = activeEnv().find(stmt.loopVar);
    std::unique_ptr<InlineBinding> boundSavep
        = boundIt != activeEnv().end() ? std::make_unique<InlineBinding>(boundIt->second)
                                       : nullptr;
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
    if (boundSavep) activeEnv()[stmt.loopVar] = *boundSavep;
    m_loops.pop_back();
    return headp;
}

AstNode* Lowering::lowerWhile(const Stmt& stmt) {
    unsupportedError(stmt.flp, stmt.kind == Stmt::WHILE ? "while loop" : "bare loop");
    return nullptr;
}

//######################################################################
// Subprograms: lookup, function inlining, procedure expansion

void Lowering::setScope(const ScopeSave& saved) {
    m_scope = saved.scope;
    m_consts = saved.consts;
    m_types = saved.types;
    m_constValues = saved.constValues;
    m_constTypes = saved.constTypes;
    m_enumConsts = saved.enumConsts;
    m_subprograms = saved.subprograms;
}

void Lowering::registerSubprograms(const std::vector<SubprogramDecl>& decls, int frame,
                                   bool processLocal) {
    for (const SubprogramDecl& decl : decls) {
        m_imported.erase(decl.name);  // The local declaration shadows the package member
        m_subprograms[decl.name].push_back(SubprogramRef{&decl, processLocal, frame});
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

bool Lowering::subprogramMatches(const Expr& call, const SubprogramDecl& sub,
                                 std::string& why) const {
    // A formal declaration can declare several names; positional actuals fill them in order
    std::vector<std::pair<const FormalDecl*, std::string>> slots;
    for (const FormalDecl& formal : sub.formals)
        for (const auto& name : formal.names) slots.emplace_back(&formal, name.first);
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
                                            const SubprogramRef** foundpp) {
    std::string name;
    std::vector<const SubprogramRef*> refs;
    if (call.a && call.a->kind == Expr::SELECTED && call.a->a && call.a->a->kind == Expr::NAME) {
        // A qualified call `package.name(...)`: consult that package
        name = call.a->text;
        importPackage(call.a->a->text);
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
    bool impureSeen = false;
    for (const SubprogramRef* const refp : refs) {
        const SubprogramDecl& sub = *refp->declp;
        if ((sub.kind == SubprogramDecl::FUNCTION) != function) continue;
        if (!sub.hasBody) {
            withoutBody = true;
            continue;
        }
        if (function && sub.impure) {
            impureSeen = true;
            continue;
        }
        std::string why;
        if (!subprogramMatches(call, sub, why)) {
            if (firstWhy.empty()) firstWhy = why;
            continue;
        }
        if (foundpp) *foundpp = refp;
        return &sub;
    }
    if (withoutBody) {
        unsupportedError(call.flp, "call to subprogram '" + name + "' whose body was not read");
    } else if (impureSeen) {
        unsupportedError(call.flp, "impure function call '" + name + "'");
    } else if (!refs.empty() && !firstWhy.empty()) {
        unsupportedError(call.flp,
                         "arguments to '" + name + "' match no declaration: " + firstWhy);
    }
    return nullptr;
}

//! A type mark that has no width until a constraint or context gives it one
static bool isUnconstrainedMark(const std::string& mark) {
    return mark == "std_logic_vector" || mark == "std_ulogic_vector" || mark == "bit_vector"
           || mark == "unsigned" || mark == "signed" || mark == "string";
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
    const auto it = activeEnv().find(name);
    if (it == activeEnv().end()) return nullptr;
    const InlineBinding& binding = it->second;
    if (write && !binding.lvalue) {
        flp->v3error("VHDL: cannot assign to formal argument '" + name + "'");
        return nullptr;
    }
    if (!write && !binding.readable) {
        flp->v3error("VHDL: out mode parameter '" + name
                     + "' may not be read before the procedure returns");
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
                                     AstNode*& prologsp, bool lvalueFormals, bool concurrent) {
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
        const bool constrained = formalp->type.hasRange || !formalp->type.constraintMark.empty();
        const VType* ctxp
            = constrained && resolveType(formalp->type, formalType) ? &formalType : nullptr;
        AstNodeExpr* actualp = asLvalue ? lowerDesignator(actual, true) : lowerExpr(actual, ctxp);
        if (!actualp) return false;
        InlineBinding binding{actualp, asLvalue, true};
        binding.type = typeOfDType(actualp->dtypep());
        if (asLvalue && formalp->mode == PortMode::OUT && formalp->cls == FormalDecl::SIGNAL) {
            // Reading an out signal formal before return would see the actual's old value
            binding.readable = false;
        }
        // An out parameter starts at its default value; a concurrent call's body drives
        // the actual with continuous assignments, so a prologue would add a second driver
        if (asLvalue && formalp->mode == PortMode::OUT && !concurrent) {
            FileLine* const pflp = formalp->flp;
            AstNodeExpr* const defp = resizeExpr(makeIntConst(pflp, 0), binding.type, pflp);
            AstNode* const initp = formalp->cls == FormalDecl::SIGNAL
                                       ? static_cast<AstNode*>(new AstAssignDly{
                                             pflp, actualp->cloneTree(false), defp})
                                       : static_cast<AstNode*>(
                                             new AstAssign{pflp, actualp->cloneTree(false), defp});
            prologsp = prologsp ? AstNode::addNext<AstNode, AstNode>(prologsp, initp) : initp;
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
        InlineBinding binding{initp, false, true};
        binding.type = typeOfDType(initp->dtypep());
        env[slots[s].second] = std::move(binding);
    }
    return true;
}

AstNodeExpr* Lowering::lowerFunctionCall(const Expr& call, const VType* context) {
    const SubprogramRef* refp = nullptr;
    const SubprogramDecl* const subp = resolveCall(call, true, &refp);
    if (!subp) return nullptr;  // resolveCall reported why
    if (!m_inlineBodies.insert(subp).second) {
        unsupportedError(call.flp, "recursive call of function '" + subp->spelling + "'");
        return nullptr;
    }
    InlineEnv env;
    AstNode* unusedPrologsp = nullptr;  // A function has no out mode formal
    // The captured environment supplies enclosing names; formals bind on top of them
    ScopeSave base;
    captureBaseFor(*refp, base, env);
    if (!bindSubprogramFormals(call, *subp, env, unusedPrologsp, false, false)) {
        m_inlineBodies.erase(subp);
        deleteInlineEnv(env);
        return nullptr;
    }
    const ScopeSave outer = saveScope();
    InlineEnv* const savedInlineP = m_inlineP;
    m_inlineFrames.push_back(InlineFrame{subp, outer, ScopeSave{}, &env});
    InlineFrame& frameRef = m_inlineFrames.back();
    setScope(base);
    m_inlineP = &env;
    const auto fail = [&](AstNodeExpr* nullp) -> AstNodeExpr* {
        deleteInlineEnv(env);
        m_inlineFrames.pop_back();
        m_inlineP = savedInlineP;
        restoreScope(outer);
        m_inlineBodies.erase(subp);
        return nullp;
    };

    for (const TypeDecl& typeDecl : subp->types) registerType(typeDecl);
    registerSubprograms(subp->subprograms, static_cast<int>(m_inlineFrames.size() - 1));
    // An alias denotes its designator; its subtype mark gives it bounds of its own
    for (const AliasDecl& alias : subp->aliases) {
        if (!alias.designator) continue;
        AstNodeExpr* const valuep = lowerExpr(*alias.designator, nullptr);
        if (!valuep) return fail(nullptr);
        InlineBinding binding{valuep, false, true};
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
    for (const Decl& decl : subp->decls) {
        VType declType;
        const bool resolved = resolveType(decl.type, declType);
        AstNodeExpr* initp = nullptr;
        if (decl.init) {
            initp = lowerExpr(*decl.init, resolved ? &declType : nullptr);
        } else if (resolved) {
            initp = makeVectorConst(decl.flp, declType, 0);
        }
        if (!initp) return fail(nullptr);
        bool first = true;
        for (const auto& name : decl.names) {
            InlineBinding binding{first ? initp : initp->cloneTree(false), false, true};
            binding.type = resolved ? declType : typeOfDType(binding.exprp->dtypep());
            env[name.first] = std::move(binding);
            first = false;
        }
    }
    VType retType;
    const VType* const outerRetp = m_funcRetp;
    // An unconstrained array return type takes its width from the body and the context
    const bool retSized = !isUnconstrainedMark(subp->returnType.mark) || subp->returnType.hasRange
                          || !subp->returnType.constraintMark.empty();
    m_funcRetp = retSized && resolveType(subp->returnType, retType) ? &retType : nullptr;
    const InlineCont noCont = [](InlineEnv&) -> AstNodeExpr* { return nullptr; };
    AstNodeExpr* resultp = evalFuncBlock(subp->body, 0, env, noCont, nullptr, 0);
    m_funcRetp = outerRetp;
    if (resultp && retType.dtp && retType.width > 0) {
        // The return expression was lowered without the return type; normalize it so an
        // enum-backed std_ulogic adopts the one-bit logic type and vice versa
        if (resultp->width() == retType.width) {
            resultp->dtypep(retType.dtp);
        } else {
            resultp = resizeExpr(resultp, retType, call.flp);
        }
    }
    if (resultp && context && context->dtp && resultp->width() == context->width) {
        resultp->dtypep(context->dtp);
    }
    return fail(resultp);
}

AstNodeExpr* Lowering::evalFuncBlock(const std::vector<StmtP>& stmts, std::size_t index,
                                     InlineEnv& env, const InlineCont& cont,
                                     const InlineCont* exitContp, int depth) {
    InlineEnv* const savedInlineP = m_inlineP;
    m_inlineP = &env;
    if (depth > 64) {
        unsupportedError(m_inlineFrames.back().declp->flp,
                         "VHDL function statement nesting is too deep");
        m_inlineP = savedInlineP;
        return nullptr;
    }
    AstNodeExpr* const resultp = evalFuncBlockBody(stmts, index, cont, exitContp, depth);
    m_inlineP = savedInlineP;
    return resultp;
}

AstNodeExpr* Lowering::evalFuncBlockBody(const std::vector<StmtP>& stmts, std::size_t index,
                                         const InlineCont& cont, const InlineCont* exitContp,
                                         int depth) {
    for (std::size_t i = index; i < stmts.size(); ++i) {
        const Stmt& stmt = *stmts[i];
        FileLine* const flp = stmt.flp;
        if (stmt.kind == Stmt::VAR_ASSIGN) {
            const Expr& target = *stmt.target;
            if (target.kind == Expr::NAME) {
                const auto it = activeEnv().find(target.text);
                if (it == activeEnv().end()) {
                    unsupportedError(flp, "assignment to a name that is not local to the "
                                          "inlined subprogram");
                    return nullptr;
                }
                // The target constrains the value, e.g. an aggregate's width
                AstNodeExpr* const valuep
                    = lowerExpr(*stmt.value, it->second.type.dtp ? &it->second.type : nullptr);
                if (!valuep) return nullptr;
                it->second.exprp->deleteTree();
                it->second.exprp = valuep;
                continue;
            }
            // Locals are bound to values: rebuild the whole value around the element update
            const Expr* basep = nullptr;
            if ((target.kind == Expr::INDEX || target.kind == Expr::SLICE) && target.a
                && target.a->kind == Expr::NAME) {
                basep = target.a.get();
            }
            if (!basep) {
                unsupportedError(flp, "assignment target form in an inlined function body");
                return nullptr;
            }
            const auto it = activeEnv().find(basep->text);
            if (it == activeEnv().end()) {
                unsupportedError(flp, "assignment to an element outside the inlined subprogram");
                return nullptr;
            }
            AstNodeExpr* const readp = lowerDesignator(target, false);
            AstSel* const selp = VN_CAST(readp, Sel);
            if (!selp) {
                if (readp) readp->deleteTree();
                unsupportedError(flp, "assignment to an element of a non-vector value");
                return nullptr;
            }
            const VType elemType = typeOfDType(selp->dtypep());
            AstNodeExpr* valuep = lowerExpr(*stmt.value, &elemType);
            if (!valuep) {
                readp->deleteTree();
                return nullptr;
            }
            AstNodeExpr* const updatedp = updateLocalElement(
                flp, it->second.exprp->cloneTree(false), valuep, selp->dtypep(),
                selp->lsbp()->cloneTree(false), selp->widthConst());
            readp->deleteTree();
            it->second.exprp->deleteTree();
            it->second.exprp = updatedp;
            continue;
        }
        if (stmt.kind == Stmt::REPORT) continue;
        if (stmt.kind == Stmt::RETURN) {
            if (!stmt.value) {
                unsupportedError(flp, "return without a value in a VHDL function body");
                return nullptr;
            }
            return lowerExpr(*stmt.value, m_funcRetp);
        }
        if (stmt.kind == Stmt::IF || stmt.kind == Stmt::CASE || stmt.kind == Stmt::FOR) {
            // Control statements continue with the rest of this statement list
            const InlineCont restCont = [&](InlineEnv& restEnv) -> AstNodeExpr* {
                return evalFuncBlock(stmts, i + 1, restEnv, cont, exitContp, depth);
            };
            if (stmt.kind == Stmt::IF) {
                return evalFuncBranches(stmt, 0, restCont, exitContp, depth + 1);
            }
            if (stmt.kind == Stmt::CASE) return evalFuncCase(stmt, restCont, exitContp, depth + 1);
            return evalFuncFor(stmt, restCont, depth + 1);
        }
        if (stmt.kind == Stmt::EXIT || stmt.kind == Stmt::NEXT) {
            AstNodeExpr* const condExprp = stmt.value ? lowerCond(*stmt.value) : nullptr;
            if (stmt.value && !condExprp) return nullptr;
            AstNodeExpr* targetp = nullptr;
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
        unsupportedError(flp, "this statement form in an inlined VHDL function body");
        return nullptr;
    }
    AstNodeExpr* const resultp = cont(activeEnv());
    if (!resultp) {
        unsupportedError(m_inlineFrames.back().declp->flp,
                         "VHDL function does not return a value on all paths");
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
    InlineEnv thenEnv = cloneInlineEnv(activeEnv());
    AstNodeExpr* thenp = evalFuncBlock(branch.second, 0, thenEnv, cont, exitContp, depth);
    AstNodeExpr* elsep = branchIndex + 1 < stmt.branches.size()
                             ? evalFuncBranches(stmt, branchIndex + 1, cont, exitContp, depth)
                             : evalFuncBlock({}, 0, activeEnv(), cont, exitContp, depth);
    deleteInlineEnv(thenEnv);
    if (!thenp || !elsep) {
        unsupportedError(stmt.flp, "VHDL function does not return a value on all paths");
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
    struct Branch final {
        AstNodeExpr* condp = nullptr;  // null for the "others" alternative
        AstNodeExpr* resultp = nullptr;
        InlineEnv env;
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
    for (const auto& item : stmt.caseItems) {
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(item.first, selectorp->dtypep(), cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            cleanup();
            selectorp->deleteTree();
            return nullptr;
        }
        Branch branch;
        branch.env = cloneInlineEnv(activeEnv());
        branch.resultp = evalFuncBlock(item.second, 0, branch.env, cont, exitContp, depth);
        if (!cops.empty()) {
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
    AstNodeExpr* resultp = nullptr;
    for (Branch& branch : branches) {
        if (branch.condp) continue;
        resultp = branch.resultp;
        branch.resultp = nullptr;
        break;
    }
    if (!resultp) {
        unsupportedError(stmt.flp, "case without 'others' in an inlined VHDL function body");
        cleanup();
        selectorp->deleteTree();
        return nullptr;
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
        left = bounds.left;
        right = bounds.right;
        up = bounds.ascending;
        if (rangep->text == "reverse_range") {
            std::swap(left, right);
            up = !up;
        }
    } else if (!staticInt(stmt.b, left) || !staticInt(stmt.c, right)) {
        unsupportedError(flp, "function loop range that is not static");
        return nullptr;
    }
    if (up ? right < left : right > left) return postCont(activeEnv());  // Empty range
    if (std::llabs(right - left) > 4096) {
        unsupportedError(flp, "function loop with more than 4096 iterations");
        return nullptr;
    }
    const int64_t step = up ? 1 : -1;
    // Each iteration becomes a continuation frame; the body's continuation is the next
    std::function<AstNodeExpr*(int64_t, InlineEnv&)> evalIteration;
    evalIteration = [&](int64_t value, InlineEnv& inEnv) -> AstNodeExpr* {
        InlineEnv iterEnv = cloneInlineEnv(inEnv);
        InlineBinding loopBinding{makeIntConst(flp, value), false, true};
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

AstNode* Lowering::lowerProcedureCall(const Expr& callExpr, bool concurrent) {
    const SubprogramRef* refp = nullptr;
    const SubprogramDecl* const subp = resolveCall(callExpr, false, &refp);
    if (!subp) {
        const std::string name
            = callExpr.kind == Expr::INDEX && callExpr.a ? callExpr.a->text : callExpr.text;
        if (!m_subprograms.count(name)) {
            callExpr.flp->v3error("VHDL: unknown subprogram '" + name + "'");
        }
        return nullptr;  // resolveCall reported the reason for a known subprogram
    }
    if (!m_inlineBodies.insert(subp).second) {
        unsupportedError(callExpr.flp, "recursive call of procedure '" + subp->spelling + "'");
        return nullptr;
    }
    InlineEnv env;
    AstNode* prologsp = nullptr;  // Out-parameter initializations
    // The captured environment supplies enclosing names; formals bind on top of them
    ScopeSave base;
    captureBaseFor(*refp, base, env);
    if (!bindSubprogramFormals(callExpr, *subp, env, prologsp, true, concurrent)) {
        m_inlineBodies.erase(subp);
        deleteInlineEnv(env);
        if (prologsp) prologsp->deleteTree();
        return nullptr;
    }
    const ScopeSave outer = saveScope();
    InlineEnv* const savedInlineP = m_inlineP;
    m_inlineFrames.push_back(InlineFrame{subp, outer, ScopeSave{}, &env});
    InlineFrame& frameRef = m_inlineFrames.back();
    frameRef.liveScope = base;
    setScope(base);
    m_inlineP = &env;
    const auto fail = [&](AstNode* nullp) -> AstNode* {
        deleteInlineEnv(env);
        m_inlineFrames.pop_back();
        m_inlineP = savedInlineP;
        restoreScope(outer);
        m_inlineBodies.erase(subp);
        if (prologsp) prologsp->deleteTree();
        return nullp;
    };

    for (const TypeDecl& typeDecl : subp->types) registerType(typeDecl);
    for (const AliasDecl& alias : subp->aliases) registerAlias(alias);
    registerSubprograms(subp->subprograms, static_cast<int>(m_inlineFrames.size() - 1));

    // Locals are real module variables reinitialized on every call, like automatics
    AstNode* initsp = nullptr;
    for (const Decl& decl : subp->decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType ltype;
        if (!resolveType(decl.type, ltype)) return fail(nullptr);
        for (const auto& name : decl.names) {
            const std::string mangled
                = "vhdlproc_" + std::to_string(++m_procVarSeq) + "__" + name.second;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, ltype.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, ltype};
            m_variables.insert(name.first);
            frameRef.liveScope = saveScope();
            AstNodeExpr* rhsp = nullptr;
            if (decl.init) rhsp = lowerExpr(*decl.init, &ltype);
            if (!rhsp) rhsp = makeVectorConst(decl.flp, ltype, 0);
            AstNodeExpr* const lhsp = new AstVarRef{decl.flp, varp, VAccess::WRITE};
            AstNode* const initp = new AstAssign{decl.flp, lhsp, rhsp};
            initsp = initsp ? AstNode::addNext<AstNode, AstNode>(initsp, initp) : initp;
        }
    }

    bool fallsThrough = true;
    AstNode* const bodysp = lowerProcChain(rawPtrs(subp->body), 0, fallsThrough);
    AstNode* resultp = prologsp;
    prologsp = nullptr;
    if (initsp) resultp = resultp ? AstNode::addNext<AstNode, AstNode>(resultp, initsp) : initsp;
    if (bodysp) resultp = resultp ? AstNode::addNext<AstNode, AstNode>(resultp, bodysp) : bodysp;
    return fail(resultp);
}

std::vector<const Stmt*> Lowering::rawPtrs(const std::vector<StmtP>& stmts) {
    std::vector<const Stmt*> out;
    out.reserve(stmts.size());
    for (const StmtP& stmtp : stmts) out.push_back(stmtp.get());
    return out;
}

//! Lower a procedure statement chain. `fallsThroughp` is false when every path returns
AstNode* Lowering::lowerProcChain(const std::vector<const Stmt*>& stmts, std::size_t index,
                                  bool& fallsThroughp) {
    AstNode* headp = nullptr;
    const auto add = [&headp](AstNode* nodep) {
        if (nodep) headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
    };
    for (std::size_t i = index; i < stmts.size(); ++i) {
        const Stmt& stmt = *stmts[i];
        if (stmt.kind == Stmt::RETURN) {
            if (stmt.value) unsupportedError(stmt.flp, "return value in a procedure body");
            fallsThroughp = false;
            return headp;
        }
        if (stmt.kind == Stmt::IF) {
            AstNode* const ifp
                = lowerProcIf(stmt, 0, {stmts.begin() + i + 1, stmts.end()}, fallsThroughp);
            add(ifp);
            return headp;
        }
        if (stmt.kind == Stmt::CASE) {
            AstNode* const casep
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
            AstNode* const setp
                = setLoopFlag(stmt.flp, isExit ? framep->exitp : framep->nextp, condp);
            add(setp);
            if (!stmt.value) {
                // An unconditional exit truncates every following statement
                fallsThroughp = false;
                return headp;
            }
            bool restFalls = true;
            AstNode* restp = lowerProcChain(stmts, i + 1, restFalls);
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
        add(lowerStmt(stmt));
    }
    fallsThroughp = true;
    return headp;
}

//! Lower the statements and then `tail`, so a return truncates the tail in that path only
AstNode* Lowering::lowerProcChainWithTail(const std::vector<StmtP>& stmts,
                                          const std::vector<const Stmt*>& tail,
                                          bool& fallsThroughp) {
    std::vector<const Stmt*> combined = rawPtrs(stmts);
    combined.insert(combined.end(), tail.begin(), tail.end());
    return lowerProcChain(combined, 0, fallsThroughp);
}

//! Lower an if/elsif/else statement, appending the following statements to each branch
AstNode* Lowering::lowerProcIf(const Stmt& stmt, std::size_t branchIndex,
                               const std::vector<const Stmt*>& tail, bool& fallsThroughp) {
    FileLine* const flp = stmt.flp;
    const auto& branch = stmt.branches[branchIndex];
    AstNodeExpr* const condp = branch.first ? lowerCond(*branch.first) : nullptr;
    if (branch.first && !condp) {
        fallsThroughp = false;
        return nullptr;
    }
    bool thenFalls = true;
    AstNode* const thensp = lowerProcChainWithTail(branch.second, tail, thenFalls);
    AstNode* elsesp = nullptr;
    bool elseFalls = true;
    if (condp && branchIndex + 1 < stmt.branches.size()) {
        elsesp = lowerProcIf(stmt, branchIndex + 1, tail, elseFalls);
    } else if (condp) {
        // A missing else falls through to the following statements
        elsesp = lowerProcChain(tail, 0, elseFalls);
    }
    fallsThroughp = thenFalls || elseFalls;
    return new AstIf{flp, condp, thensp, elsesp};
}

//! Lower a case statement, appending the following statements to each alternative
AstNode* Lowering::lowerProcCase(const Stmt& stmt, const std::vector<const Stmt*>& tail,
                                 bool& fallsThroughp) {
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
        AstNode* const bodyp = lowerProcChainWithTail(item.second, tail, altFalls);
        fallsThroughp = fallsThroughp || altFalls;
        AstCaseItem* const itemp = cops.empty() ? new AstCaseItem{flp, nullptr, bodyp}
                                                : new AstCaseItem{flp, cops.front(), bodyp};
        for (std::size_t i = 1; i < cops.size(); ++i) itemp->addCondsp(cops[i]);
        itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, itemp) : itemp;
    }
    return makeCase(flp, selectorp, itemsp);
}

void Lowering::lowerProcess(const Process& process) {
    if (!process.hasSensitivity) {
        lowerWaitProcess(process);
        return;
    }
    // The process declarative part declares variables that hide module-level names
    const ScopeSave saved = saveScope();
    std::set<std::string> varNames;
    emitProcessDecls(process, varNames);
    std::vector<AstVar*> sensVars;
    std::vector<std::string> sensNames = process.sensitivity;
    if (process.sensitivityAll) {
        // process(all) is sensitive to every signal it reads, including any clock, so
        // a clock edge in the body cannot become an edge-triggered always block
        if (stmtsContainEdge(process.body)) {
            unsupportedError(process.flp, "clock edge in a process(all)");
            return;
        }
        std::set<std::string> readNames;
        collectReadNames(process.body, readNames);
        sensNames.assign(readNames.begin(), readNames.end());
    }
    for (const std::string& name : sensNames) {
        // A variable is not a signal, so it never becomes a sensitivity item
        if (varNames.count(name) || m_variables.count(name)) continue;
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
    restoreScope(saved);
}

Lowering::ScopeSave Lowering::saveScope() const {
    ScopeSave saved;
    saved.scope = m_scope;
    saved.consts = m_consts;
    saved.types = m_types;
    saved.constValues = m_constValues;
    saved.constTypes = m_constTypes;
    saved.enumConsts = m_enumConsts;
    saved.subprograms = m_subprograms;
    return saved;
}

void Lowering::restoreScope(const ScopeSave& saved) {
    m_scope = saved.scope;
    m_consts = saved.consts;
    m_types = saved.types;
    m_constValues = saved.constValues;
    m_constTypes = saved.constTypes;
    m_enumConsts = saved.enumConsts;
    m_subprograms = saved.subprograms;
}

//! Declare the types, constants and variables of a process declarative part
void Lowering::emitProcessDecls(const Process& process, std::set<std::string>& varNames) {
    int varSeq = 0;
    for (const TypeDecl& typeDecl : process.types) registerType(typeDecl);
    registerSubprograms(process.subprograms, -1, true);
    for (const Decl& decl : process.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
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
            varNames.insert(name.first);
            m_variables.insert(name.first);
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

void Lowering::emitConstant(const std::vector<std::pair<std::string, std::string>>& names,
                            const TypeSpec& type, const Expr* initp, FileLine* flp,
                            bool deferredOk) {
    if (!initp) {
        // A package body completes the constants its specification deferred
        if (!deferredOk) flp->v3error("VHDL: constant without a value");
        return;
    }
    VType constType;
    bool intConstant = true;
    if (type.flp && resolveType(type, constType)) intConstant = constType.isInt();
    if (intConstant) {
        int64_t value = 0;
        if (evalInt(*initp, value)) {
            for (const auto& name : names) {
                if (!reserveName(name.first, name.second, flp)) continue;
                m_consts[name.first] = value;
            }
            return;
        }
        // A call to a declared function is static as well: lower and fold it below
        if (!constType.dtp) constType = intType(flp);
    }
    AstNodeExpr* valuep = lowerExpr(*initp, &constType);
    if (!valuep) return;
    valuep = V3Const::constifyEditCpp(valuep);
    if (AstConst* const foldedp = foldToConst(valuep)) valuep = foldedp;
    AstConst* const constp = VN_CAST(valuep, Const);
    if (!constp) {
        unsupportedError(flp, "constant value that is not static");
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
    }
}

//! Register the entity's generics as constants; bindings hold the bound actuals and
//! the rest fall back to their defaults. A probe binds only what a range needs.
bool Lowering::registerGenerics(const EntityUnit& entity, const GenericBindings* bindings,
                                bool probe) {
    bool ok = true;
    for (const GenericDecl& gen : entity.generics) {
        for (const auto& name : gen.names) {
            if (bindings) {
                const auto it = bindings->find(name.first);
                if (it != bindings->end()) {
                    if (it->second.isInt) {
                        m_consts[name.first] = it->second.intValue;
                    } else {
                        m_ownedConsts.push_back(
                            std::unique_ptr<AstConst>(it->second.valuep->cloneTree(false)));
                        m_constValues[name.first] = m_ownedConsts.back().get();
                        VType declared;
                        if (resolveType(gen.type, declared)) m_constTypes[name.first] = declared;
                    }
                    continue;
                }
            }
            if (!gen.init) {
                ok = false;
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

void Lowering::importPackage(const std::string& name) {
    if (!m_packagesp) return;
    const auto pkgIt = m_packagesp->packages.find(name);
    if (pkgIt == m_packagesp->packages.end()) return;  // Standard package: intrinsics
    const PackageInfo& info = pkgIt->second;
    for (const auto& alias : info.aliases) {
        if (m_aliases.count(alias.first)) continue;
        m_aliases[alias.first] = alias.second;
    }

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
    for (const SubprogramDecl* const subp : info.subprograms) {
        std::vector<SubprogramRef>& refs = m_subprograms[subp->name];
        const bool known = std::any_of(refs.begin(), refs.end(),
                                       [=](const SubprogramRef& r) { return r.declp == subp; });
        if (known) continue;
        refs.push_back(SubprogramRef{subp, false, -1});
        m_imported.insert(subp->name);
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
    m_aliases.clear();
    m_subprograms.clear();
    for (const TypeDecl& typeDecl : pkg.types) registerType(typeDecl);
    for (const AliasDecl& alias : pkg.aliases) registerAlias(alias);
    // Subprograms are visible to the declarative items below, including deferred constants
    registerSubprograms(pkg.subprograms, -1);
    for (const Decl& decl : pkg.decls)
        emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, !pkg.isBody);

    PackageInfo info;
    info.aliases = m_aliases;
    info.types = std::move(m_types);
    info.consts = std::move(m_consts);
    info.constValues = std::move(m_constValues);
    info.constTypes = std::move(m_constTypes);
    info.enumConsts = std::move(m_enumConsts);
    info.owned = std::move(m_ownedConsts);
    for (const SubprogramDecl& sub : pkg.subprograms) info.subprograms.push_back(&sub);
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ownedConsts.clear();
    m_aliases.clear();

    const auto existing = table.packages.find(pkg.name);
    if (existing == table.packages.end()) {
        table.packages.emplace(pkg.name, std::move(info));
        return;
    }
    // A package body (or a repeated declaration) adds to what is already published
    PackageInfo& dest = existing->second;
    for (const auto& alias : info.aliases) dest.aliases[alias.first] = alias.second;
    for (const auto& type : info.types) dest.types[type.first] = type.second;
    for (const auto& constant : info.consts) dest.consts[constant.first] = constant.second;
    for (const auto& value : info.constValues) dest.constValues[value.first] = value.second;
    for (const auto& type : info.constTypes) dest.constTypes[type.first] = type.second;
    for (const auto& literal : info.enumConsts) dest.enumConsts[literal.first] = literal.second;
    for (std::unique_ptr<AstConst>& constp : info.owned) dest.owned.push_back(std::move(constp));
    for (const SubprogramDecl* const subp : info.subprograms) dest.subprograms.push_back(subp);
}

bool Lowering::emit(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
                    const GenericBindings* bindings) {
    m_modp = modp;

    m_subprograms.clear();  // Packages published these only through importPackage
    for (const auto& use : entity.uses) importPackage(use.second);
    for (const auto& use : arch.uses) importPackage(use.second);

    // Types are visible to the whole declarative part, and must be registered first
    for (const TypeDecl& typeDecl : entity.types) registerType(typeDecl);
    for (const TypeDecl& archType : arch.types) registerType(archType);

    // Generics are constants visible to the port clause and the architecture
    if (!registerGenerics(entity, bindings, false)) {
        m_modp = nullptr;
        m_scope.clear();
        m_consts.clear();
        m_constValues.clear();
        m_constTypes.clear();
        m_enumConsts.clear();
        m_ownedConsts.clear();
        m_types.clear();
        return false;
    }

    for (const AliasDecl& alias : arch.aliases) registerAlias(alias);
    registerSubprograms(entity.subprograms, -1);
    registerSubprograms(arch.subprograms, -1);

    // Preliminary snapshot: declarative items may already call subprograms
    m_moduleScope = saveScope();

    // Entity declarative items are visible to every architecture of the entity
    int entityVarSeq = 0;
    for (const Decl& decl : entity.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            const std::string mangled
                = "vhdlext_" + std::to_string(++entityVarSeq) + "__" + name.second;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            m_variables.insert(name.first);
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
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, name.second, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.kind == Decl::VARIABLE) m_variables.insert(name.first);
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

    // Subprograms of the declarative part capture the module scope as it stands here
    m_moduleScope = saveScope();
    for (const auto& stmtp : arch.stmts) emitConcurrent(*stmtp);

    m_modp = nullptr;
    m_scope.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ownedConsts.clear();
    m_types.clear();
    return true;
}

//! Type marks whose subtype a generic's actual can be checked against directly
static bool isBuiltinGenericMark(const std::string& mark) {
    static const char* const marks[]
        = {"integer",   "natural",    "positive",         "boolean",           "bit",
           "std_logic", "std_ulogic", "std_logic_vector", "std_ulogic_vector", "bit_vector",
           "unsigned",  "signed",     "u_unsigned",       "u_signed"};
    for (const char* const candidate : marks) {
        if (mark == candidate) return true;
    }
    return false;
}

//! Bind an instance's generics and return the module specialized for that binding
AstModule* Lowering::specializeInstance(const Concurrent& conc, const EntityUnit& entity,
                                        AstModule* basep) {
    FileLine* const flp = conc.flp;
    struct Item final {
        const GenericDecl* declp;
        std::string folded;
        std::string spelling;
    };
    std::vector<Item> items;
    for (const GenericDecl& gen : entity.generics) {
        for (const auto& name : gen.names) items.push_back({&gen, name.first, name.second});
    }
    const std::vector<PortAssoc>& assocs = conc.genericAssociations;
    const bool positional = std::any_of(
        assocs.begin(), assocs.end(), [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    auto freeBindings = [](GenericBindings& bs) {
        for (auto& entry : bs) {
            if (entry.second.valuep) entry.second.valuep->deleteTree();
        }
    };
    if (positional && assocs.size() > items.size()) {
        flp->v3error("VHDL: instance of '" + entity.spelling
                     + "' binds more generics than the entity has");
        return nullptr;
    }
    if (!positional) {
        for (const PortAssoc& assoc : assocs) {
            const bool known = std::any_of(items.begin(), items.end(), [&](const Item& item) {
                return item.folded == assoc.formal;
            });
            if (!known) {
                assoc.flp->v3error("VHDL: entity '" + entity.spelling + "' has no generic '"
                                   + assoc.formalSpelling + "'");
                return nullptr;
            }
        }
    }
    GenericBindings bindings;
    for (size_t k = 0; k < items.size(); ++k) {
        const PortAssoc* assocp = nullptr;
        if (positional) {
            if (k < assocs.size()) assocp = &assocs[k];
        } else {
            for (const PortAssoc& assoc : assocs) {
                if (assoc.formal == items[k].folded) assocp = &assoc;
            }
        }
        const GenericDecl& gen = *items[k].declp;
        if (!assocp) {
            if (!gen.init) {
                flp->v3error("VHDL: generic '" + items[k].spelling + "' of entity '"
                             + entity.spelling + "' has no default value and no association");
                freeBindings(bindings);
                return nullptr;
            }
            continue;
        }
        if (!assocp->actual) {
            assocp->flp->v3error("VHDL: 'open' is not allowed on a generic association");
            freeBindings(bindings);
            return nullptr;
        }
        if (gen.type.mark == "string") {
            unsupportedError(assocp->flp, "string generic");
            freeBindings(bindings);
            return nullptr;
        }
        // The declared type resolves in the entity's scope, where the generics that
        // precede this one already carry the value bound at this instance
        ScopeSave saved = saveScope();
        const std::set<std::string> savedImported = m_imported;
        for (const auto& use : entity.uses) importPackage(use.second);
        for (const TypeDecl& typeDecl : entity.types) registerType(typeDecl);
        registerGenerics(entity, &bindings, true);
        VType declared;
        const bool haveType = (isBuiltinGenericMark(gen.type.mark) || m_types.count(gen.type.mark))
                              && resolveType(gen.type, declared);
        restoreScope(saved);
        m_imported = savedImported;
        if (!haveType && isBuiltinGenericMark(gen.type.mark)) {
            unsupportedError(gen.flp, "generic of type '" + gen.type.mark + "'");
            freeBindings(bindings);
            return nullptr;
        }
        if (!haveType) {
            unsupportedError(assocp->flp, "generic of type '" + gen.type.mark + "'");
            freeBindings(bindings);
            return nullptr;
        }
        GenericBinding binding;
        bool bound = false;
        if (declared.isInt()) {
            bound = evalInt(*assocp->actual, binding.intValue);
            binding.isInt = bound;
        }
        if (!bound) {
            AstNodeExpr* const valuep = lowerExpr(*assocp->actual, &declared);
            AstConst* const foldedp = valuep ? foldToConst(valuep) : nullptr;
            if (foldedp) {
                binding.valuep = foldedp;
                bound = true;
            } else {
                if (valuep) valuep->deleteTree();
                if (!V3Error::errorCount()) {
                    unsupportedError(assocp->flp, "generic actual that is not static");
                }
                freeBindings(bindings);
                return nullptr;
            }
        }
        bindings[items[k].folded] = binding;
    }
    if (bindings.empty()) return basep;  // Every generic keeps its default
    std::string key = entity.name;
    for (const auto& entry : bindings) {
        key += "|" + entry.first + "=";
        if (entry.second.isInt)
            key += std::to_string(entry.second.intValue);
        else
            key += entry.second.valuep->num().ascii(false);
    }
    auto& cache = m_designp->specializations;
    const auto found = cache.find(key);
    if (found != cache.end()) {
        AstModule* const cachedp = found->second;
        freeBindings(bindings);
        return cachedp;  // Null when an earlier specialization of this key failed
    }
    if (++m_designp->specDepth > 32) {
        unsupportedError(flp, "generic binding recursion that does not terminate");
        freeBindings(bindings);
        return nullptr;
    }
    const auto archIt = m_designp->archs.find(entity.name);
    if (archIt == m_designp->archs.end()) {
        flp->v3error("VHDL: entity '" + entity.spelling
                     + "' has no architecture to specialize for this instance");
        cache[key] = nullptr;
        freeBindings(bindings);
        return nullptr;
    }
    AstModule* const specp = new AstModule{
        entity.flp, entity.spelling + "__Vgen" + std::to_string(++m_designp->specSeq),
        entity.libname};
    specp->inLibrary(entity.inLibrary);
    m_rootp->addModulesp(specp);
    Lowering child{m_rootp, m_packagesp, m_modulesp, m_designp};
    const bool ok = child.emit(entity, *archIt->second, specp, &bindings);
    --m_designp->specDepth;
    freeBindings(bindings);
    cache[key] = ok ? specp : nullptr;
    return ok ? specp : nullptr;
}

//! An instantiation becomes an AstCell bound to the instantiated entity's module
AstNode* Lowering::lowerInstance(const Concurrent& conc) {
    FileLine* const flp = conc.flp;
    if (!m_modulesp) {
        unsupportedError(flp, "instantiation without an elaborated design");
        return nullptr;
    }
    const auto modIt = m_modulesp->find(conc.instanceName);
    if (modIt == m_modulesp->end()) {
        // Same wording as the GHDL path, so one golden serves both frontends
        flp->v3error("Cannot find VHDL entity or module '" + conc.instanceName + "' for instance '"
                     + conc.instanceLabel + "'");
        return nullptr;
    }
    AstModule* modp = modIt->second;
    const EntityUnit* entityp = nullptr;
    if (m_designp) {
        const auto entityIt = m_designp->entities.find(conc.instanceName);
        if (entityIt != m_designp->entities.end()) entityp = entityIt->second;
    }
    if (!conc.genericAssociations.empty() && (!entityp || entityp->generics.empty())) {
        flp->v3error("VHDL: generic map on instance '" + conc.instanceLabel + "' of '"
                     + conc.instanceName + "', which has no generics");
        return nullptr;
    }
    if (entityp && !entityp->generics.empty()) {
        modp = specializeInstance(conc, *entityp, modp);
        if (!modp) return nullptr;
    }
    std::vector<AstVar*> ports;
    for (AstNode* const itemp : modp->stmtspList()) {
        AstVar* const varp = VN_CAST(itemp, Var);
        if (varp && varp->isIO()) ports.push_back(varp);
    }
    std::sort(ports.begin(), ports.end(),
              [](const AstVar* l, const AstVar* r) { return l->pinNum() < r->pinNum(); });
    // Associations without a formal name are positional: they map to the next port
    const bool positional
        = std::any_of(conc.associations.begin(), conc.associations.end(),
                      [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    if (positional && conc.associations.size() > ports.size()) {
        flp->v3error("VHDL: instance of '" + conc.instanceName
                     + "' associates more ports than "
                       "the entity has");
        return nullptr;
    }
    //! One association of a port, resolved to the bit range it connects
    struct Connection final {
        int lsb = 0;
        int width = 1;
        const PortAssoc* assocp = nullptr;
    };
    AstPin* pinsp = nullptr;
    for (size_t portIndex = 0; portIndex < ports.size(); ++portIndex) {
        AstVar* const portp = ports[portIndex];
        std::vector<const PortAssoc*> assocs;
        if (positional) {
            if (portIndex < conc.associations.size())
                assocs.push_back(&conc.associations[portIndex]);
        } else {
            for (const PortAssoc& assoc : conc.associations) {
                if (assoc.formalSpelling == portp->name()) assocs.push_back(&assoc);
            }
        }
        if (assocs.empty()) {
            // Unassociated port: an open pin
            AstPin* const pinp = new AstPin{flp, portp->pinNum(), portp->name(), nullptr};
            pinp->modVarp(portp);
            pinsp = pinsp ? AstNode::addNext<AstPin, AstPin>(pinsp, pinp) : pinp;
            continue;
        }
        const bool partial
            = std::any_of(assocs.begin(), assocs.end(),
                          [](const PortAssoc* assocp) { return assocp->hasFormalRange; });
        const VType portType = typeOfDType(portp->dtypep());
        AstNodeExpr* actualp = nullptr;
        if (!partial) {
            // The whole port connects to one actual
            const PortAssoc& assoc = *assocs.front();
            if (assoc.actual) {
                const bool driven = portp->direction().isOutput() || portp->direction().isInout();
                AstNodeExpr* valuep = driven ? lowerDesignator(*assoc.actual, true)
                                             : lowerExpr(*assoc.actual, &portType);
                if (!valuep) return nullptr;
                actualp = resizeExpr(valuep, portType, flp);
            }
        } else if (!portType.isVector() || portType.elemWidth != 1) {
            unsupportedError(flp, "part of a port that is not a bit vector");
            return nullptr;
        } else if (portp->direction().isInout()) {
            unsupportedError(flp, "part of an inout port");
            return nullptr;
        } else {
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
                    assocp->flp->v3error("VHDL: association is out of range of port '"
                                         + portp->name() + "'");
                    bad = true;
                    break;
                }
                connection.assocp = assocp;
                connections.push_back(connection);
            }
            if (bad) return nullptr;
            if (portp->direction().isInput()) {
                // Concatenate from the most significant connected range down
                std::sort(connections.begin(), connections.end(),
                          [](const Connection& lhs, const Connection& rhs) {
                              return lhs.lsb > rhs.lsb;
                          });
                for (const Connection& connection : connections) {
                    AstNodeExpr* valuep = nullptr;
                    if (connection.assocp->actual) {
                        const VType elementType
                            = vectorType(flp, portType.kwd, VSigning::UNSIGNED, connection.width);
                        valuep = lowerExpr(*connection.assocp->actual, &elementType);
                        if (!valuep) return nullptr;
                        valuep = resizeExpr(valuep, elementType, flp);
                    } else {
                        // An open input element contributes unknown bits
                        valuep = makeRepeatedConst(
                            flp,
                            vectorType(flp, portType.kwd, VSigning::UNSIGNED, connection.width),
                            'x');
                    }
                    actualp = actualp
                                  ? static_cast<AstNodeExpr*>(new AstConcat{flp, actualp, valuep})
                                  : valuep;
                }
                actualp->dtypep(portp->dtypep());
            } else {
                // Outputs: each element actual is driven from its slice of a temp
                AstVar* const tempVarp
                    = new AstVar{flp, VVarType::VAR,
                                 "vhdlind_" + std::to_string(++m_loopSeq) + "__" + portp->name(),
                                 portp->dtypep()};
                m_modp->addStmtsp(tempVarp);
                AstNode* headp = nullptr;
                for (const Connection& connection : connections) {
                    if (!connection.assocp->actual) continue;
                    AstNodeExpr* const lhsp = lowerDesignator(*connection.assocp->actual, true);
                    if (!lhsp) {
                        if (headp) headp->deleteTree();
                        return nullptr;
                    }
                    const VType elementType
                        = vectorType(flp, portType.kwd, VSigning::UNSIGNED, connection.width);
                    AstSel* const rhsp
                        = new AstSel{flp, new AstVarRef{flp, tempVarp, VAccess::READ},
                                     connection.lsb, connection.width};
                    rhsp->dtypep(elementType.dtp);
                    AstNode* const assp
                        = new AstAssign{flp, resizeExpr(lhsp, elementType, flp), rhsp};
                    headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, assp) : assp;
                }
                if (headp) {
                    m_modp->addStmtsp(new AstAlways{flp, VAlwaysKwd::CONT_ASSIGN, nullptr, headp});
                }
                actualp = new AstVarRef{flp, tempVarp, VAccess::WRITE};
            }
        }
        AstPin* const pinp = new AstPin{flp, portp->pinNum(), portp->name(), actualp};
        pinp->modVarp(portp);
        pinsp = pinsp ? AstNode::addNext<AstPin, AstPin>(pinsp, pinp) : pinp;
    }
    if (!positional) {
        for (const PortAssoc& assoc : conc.associations) {
            const bool known = std::any_of(ports.begin(), ports.end(), [&](const AstVar* portp) {
                return portp->name() == assoc.formalSpelling;
            });
            if (!known) {
                flp->v3error("VHDL: entity '" + conc.instanceName + "' has no port '"
                             + assoc.formalSpelling + "'");
            }
        }
    }
    AstCell* const cellp = new AstCell{
        flp, flp, conc.instanceLabel + m_genSuffix, modp->name(), pinsp, nullptr, nullptr};
    return cellp;
}

//! Declare the own signals, variables, constants and types of a block or generate
void Lowering::emitLocalDecls(const std::vector<Decl>& decls, const std::vector<TypeDecl>& types,
                              const char* prefix) {
    for (const TypeDecl& typeDecl : types) registerType(typeDecl);
    for (const Decl& decl : decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            const std::string mangled
                = std::string{prefix} + std::to_string(++m_loopSeq) + "__" + name.second;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
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
            rhsp = resizeExpr(rhsp, type, decl.flp);
            m_modp->addStmtsp(new AstInitialStatic{decl.flp, new AstAssign{decl.flp, lhsp, rhsp}});
        }
    }
}

void Lowering::emitConcurrent(const Concurrent& conc) {
    const Concurrent& stmt = conc;
    if (stmt.kind == Concurrent::ASSERT) return;  // Synthesis ignores assertions
    if (stmt.kind == Concurrent::CALL) {
        // A concurrent procedure call is lowered with continuous assignments
        ++m_concurrentProc;
        if (AstNode* const callp = lowerProcedureCall(*stmt.value, true)) m_modp->addStmtsp(callp);
        --m_concurrentProc;
        return;
    }
    if (stmt.kind == Concurrent::GEN_FOR) {
        // A generate loop is unrolled with its parameter bound as a constant
        int64_t left = 0;
        int64_t right = 0;
        if (!staticInt(stmt.lo, left) || !staticInt(stmt.hi, right)) {
            unsupportedError(stmt.flp, "generate range that is not static");
            return;
        }
        const bool up = stmt.rangeAscending;
        if (up ? right < left : right > left) return;  // Empty range
        if (std::llabs(right - left) > 4096) {
            unsupportedError(stmt.flp, "generate range with more than 4096 iterations");
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
            emitLocalDecls(stmt.decls, stmt.types, "vhdlgen_");
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
            AstConst* const foldedp
                = condp ? foldToConst(V3Const::constifyEditCpp(condp)) : nullptr;
            if (!foldedp) {
                unsupportedError(stmt.flp, "generate condition that is not static");
                return;
            }
            if (foldedp->num().mostSetBitP1() == 1 && foldedp->num().toUInt()) {
                takenp = &branch;
                break;
            }
        }
        if (!takenp) return;  // No condition was true and there is no else part
        const ScopeSave saved = saveScope();
        const std::string savedSuffix = m_genSuffix;
        m_genSuffix = savedSuffix + "__gen" + std::to_string(++m_genSeq);
        emitLocalDecls(takenp->decls, takenp->types, "vhdlgen_");
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
        emitLocalDecls(stmt.decls, stmt.types, "vhdlblk_");
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
    if (stmt.kind == Concurrent::SEL_ASSIGN) {
        AstNode* const casep = lowerSelected(stmt);
        if (!casep) return;
        m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, casep});
        return;
    }
    AstNodeExpr* const lhsp = lowerDesignator(*stmt.target, true);
    if (!lhsp) return;
    const VType ltype = typeOfTarget(*stmt.target, lhsp);
    if (stmt.kind == Concurrent::COND_ASSIGN && concHasEdge(stmt)) {
        // Register clocked by the edge the condition names
        m_edges.clear();
        m_initial = false;
        // Names tested before the clocked branch, e.g. an asynchronous reset
        std::set<std::string> levelNames;
        for (const auto& alt : stmt.alternatives) {
            if (alt.second && containsEdgeCall(*alt.second)) break;
            if (alt.second) collectExprNames(*alt.second, levelNames);
        }
        AstNode* const bodyp = lowerClockedAlternatives(stmt, ltype, lhsp);
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
        m_modp->addStmtsp(
            new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS, new AstSenTree{stmt.flp, itemsp}, bodyp});
        lhsp->deleteTree();
        return;
    }
    AstNodeExpr* rhsp = nullptr;
    if (stmt.kind == Concurrent::COND_ASSIGN) {
        rhsp = lowerAlternatives(stmt, ltype);
    } else {
        rhsp = lowerExpr(*stmt.value, &ltype);
        if (rhsp) rhsp = resizeExpr(rhsp, ltype, stmt.flp);
    }
    if (!rhsp) {
        lhsp->deleteTree();
        return;
    }
    m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                                    new AstAssignW{stmt.flp, lhsp, rhsp}});
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

//! The netlist the modules belong to, for the dtype table
static AstNetlist* rootpFromModules(const ModuleMap& modules) {
    (void)modules;
    return v3Global.rootp();
}

void createModules(const std::vector<EntityUnit>& entities, AstNetlist* rootp,
                   ModuleMap& modules) {
    for (const EntityUnit& entity : entities) {
        if (modules.count(entity.name)) continue;
        AstModule* const modp = new AstModule{entity.flp, entity.spelling, entity.libname};
        modp->inLibrary(entity.inLibrary);
        rootp->addModulesp(modp);
        modules[entity.name] = modp;
    }
}

bool lower(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
           const PackageTable& table, const ModuleMap& modules, Design& design) {
    Lowering lowering{rootpFromModules(modules), &table.impl(), &modules, &design};
    return lowering.emit(entity, arch, modp);
}

}  // namespace vhdlnative
