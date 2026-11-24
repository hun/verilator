// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL frontend lowering: internal declarations
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
#ifndef VERILATOR_V3VHDLNATIVELOWERIMPL_H_
#define VERILATOR_V3VHDLNATIVELOWERIMPL_H_

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3VhdlNativeLower.h"
#include "V3VhdlNativeTypes.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vhdlnative {

//! Maximum iterations folded out of a static loop or generate
static constexpr int64_t MAX_FOLD_ITERATIONS = 4096;
static constexpr int MAX_RECURSION_DEPTH = 64;  // Nested inlinings of one function
//! Maximum width of a vector expression folded by a conversion
static constexpr int64_t MAX_FOLD_WIDTH = 4096;
//! Maximum width of a constant folded into a memory image
static constexpr int MAX_FOLD_CONST_WIDTH = 1 << 20;
//! Maximum AST cost of the element writes folded into one function body
static constexpr int64_t MAX_FOLD_WRITE_COST = 1 << 18;

struct AliasInfo final {
    const Expr* designatorp = nullptr;  // Aliased object, slice or subprogram
    VType type;  // Subtype of the alias; see hasType
    bool hasType = false;  // The alias has a subtype indication
};

struct PackageInfo final {
    std::map<std::string, AliasInfo> aliases;  // Aliases by folded name
    std::map<std::string, VType> types;  // Declared types by folded name
    std::map<std::string, int64_t> consts;  // Integer constants by folded name
    std::map<std::string, AstConst*> constValues;  // Typed constants by folded name
    std::map<std::string, VType> constTypes;  // Type of each typed constant
    std::map<std::string, AstConst*> enumConsts;  // Enumeration literals by folded name
    std::set<std::string> ambiguousLiterals;  // Literals declared by more than one type
    std::map<std::string, double> realConsts;  // Elaboration-time reals, by folded name
    std::vector<const SubprogramDecl*> subprograms;  // Prototypes and bodies, by declaration
    std::map<std::string, const ComponentDecl*> components;  // Folded name -> declaration
    // Declarations only the body makes: private to it, but visible while a body it
    // declares is inlined (IEEE 1076-2008 12.4)
    std::vector<const SubprogramDecl*> bodySubprograms;
    std::vector<std::pair<std::string, std::string>> uses;  // Packages its `use` made visible
    std::vector<const AliasDecl*> subprogramAliases;  // Synonyms, re-registered on import
    std::vector<std::unique_ptr<AstConst>> owned;  // Owns the constants above
};

struct PackageTable::Impl final {
    std::map<std::string, PackageInfo> packages;  // Folded package name -> contents
};

//! Statically bound value of one generic actual
struct GenericBinding final {
    bool isInt = false;  // Value is an integer, in intValue
    int64_t intValue = 0;  // Integer value, when isInt
    bool isReal = false;  // Value is a real, in realValue
    double realValue = 0.0;  // Real value, when isReal
    bool isString = false;  // Value is a string, in stringValue
    std::string stringValue;  // String value, when isString
    bool isType = false;  // Actual is a type mark, in typeValue
    VType typeValue;  // The actual type mark of a generic type parameter
    bool hasValueType = false;  // Whether valueType is set
    VType valueType;  // The declared type, specialized by the actual's bounds
    AstConst* valuep = nullptr;  // Typed constant; cloned when registered, else owned here
    const SubprogramDecl* subp = nullptr;  // The actual of a generic subprogram parameter
};
using GenericBindings = std::map<std::string, GenericBinding>;

//! Map with O(1) copies: a scope snapshot shares the table until one side writes
template <typename Value>
class CowMap final {
    using Map = std::unordered_map<std::string, Value>;

public:
    using iterator = typename Map::iterator;
    using const_iterator = typename Map::const_iterator;

    CowMap()
        : m_mapp{std::make_shared<Map>()} {}
    CowMap(const CowMap&) = default;
    CowMap& operator=(const CowMap&) = default;
    CowMap(CowMap&&) = default;
    CowMap& operator=(CowMap&&) = default;

    iterator find(const std::string& key) {
        detach();
        return m_mapp->find(key);
    }
    const_iterator find(const std::string& key) const { return m_mapp->find(key); }
    size_t count(const std::string& key) const { return m_mapp->count(key); }
    Value& operator[](const std::string& key) {
        detach();
        return (*m_mapp)[key];
    }
    void erase(const std::string& key) {
        detach();
        m_mapp->erase(key);
    }
    void clear() {
        detach();
        m_mapp->clear();
    }
    bool empty() const { return m_mapp->empty(); }
    size_t size() const { return m_mapp->size(); }
    iterator begin() {
        detach();
        return m_mapp->begin();
    }
    iterator end() {
        detach();
        return m_mapp->end();
    }
    const_iterator begin() const { return m_mapp->begin(); }
    const_iterator end() const { return m_mapp->end(); }

private:
    void detach() {
        if (m_mapp.use_count() > 1) m_mapp = std::make_shared<Map>(*m_mapp);
    }
    std::shared_ptr<Map> m_mapp;  // Shared map, copied on first write if shared
};

class Lowering final {
    AstNetlist* const m_rootp;  // Netlist to emit into
    AstModule* m_modp = nullptr;  // Module being emitted
    struct VarInfo final {
        AstVar* varp = nullptr;  // Signal or variable this name denotes
        VType type;  // Resolved type of the name
    };
    //! A visible subprogram: its declaration and whether a process declared it
    struct SubprogramRef final {
        const SubprogramDecl* declp = nullptr;  // Declaration of the subprogram
        bool processLocal = false;  // Declared inside a process
        int frame = -1;  // Active inline frame that owns it, or -1
        bool imported = false;  // Came from another unit, so a local homograph hides it
        bool ownSpec = false;  // This unit's own specification: the body completes it
    };
    //! Module-level declarations a process hides, restored when it ends
    struct ScopeSave final {
        CowMap<VarInfo> scope;  // Visible signals and ports by folded name
        std::map<std::string, int64_t> consts;  // Integer constants by folded name
        // Integer constants visible while registering types
        std::map<std::string, int64_t> preConsts;
        std::map<std::string, VType> types;  // Declared types by folded name
        std::set<std::string> ambiguousTypes;  // Type names imported from two packages
        std::set<std::string> ambiguousLiterals;  // Enumeration literals from two types
        std::map<std::string, AstConst*> constValues;  // Typed constants by folded name
        std::map<std::string, VType> constTypes;  // Type of each typed constant
        std::map<std::string, AstConst*> enumConsts;  // Enumeration literals by folded name
        // Visible subprograms by folded name
        std::map<std::string, std::vector<SubprogramRef>> subprograms;
        // Visible component declarations by folded name
        std::map<std::string, const ComponentDecl*> components;
        std::set<std::string> stdUses;  // Standard packages the context clause names
    };
    //! One name bound during subprogram inlining: an expression value, or an lvalue
    struct InlineBinding final {
        AstNodeExpr* exprp = nullptr;  // Bound expression; cloned at each use
        bool lvalue = false;  // exprp denotes a writable location (an out/inout parameter)
        bool readable = true;  // False for an out signal parameter, whose reads are rejected
        VType type;  // Resolved type of the bound value, for attributes of the name
        AstVar* materializedp = nullptr;  // A local held by a real variable, not an expression
    };
    using InlineEnv = std::map<std::string, InlineBinding>;  // Bindings while inlining
    using InlineCont = std::function<AstNodeExpr*(InlineEnv&)>;  // Continuation for inlining
    //! Continuations of active static function loops, for labeled exit/next
    struct FuncLoopTarget final {
        std::string label;  // Loop label, empty when unlabeled
        const InlineCont* nextp = nullptr;  // Continue with the following iteration
        const InlineCont* exitp = nullptr;  // Continue after the loop
    };
    //! An active inlining: the body, the scope its callee captures, and its bindings
    struct InlineFrame final {
        const SubprogramDecl* declp = nullptr;  // The subprogram body being inlined
        ScopeSave entryScope;  // The caller's scope when the call was made
        ScopeSave liveScope;  // Grows with procedure locals
        InlineEnv* envp = nullptr;  // Bindings of this inlining
        //! Structure of the array the body returns, when every return agrees on it
        VType returnedType = VType{};
        bool hasReturnedType = false;
        bool returnedTypeMixed = false;  // Two returns gave arrays of different shape
    };
    //! Active bindings, referenced before the scope tables: a callee owns its map while
    //! its body lowers, so nested lowering sees it
    InlineEnv m_ownEnv;  // Bindings outside any inlining: always empty
    InlineEnv* m_inlinep = &m_ownEnv;  // Bindings of the innermost active inlining
    InlineEnv& activeEnv() { return *m_inlinep; }
    const InlineEnv& activeEnv() const { return *m_inlinep; }
    std::deque<InlineFrame> m_inlineFrames;  // Active inlinings, innermost last
    // Loops of the active static function inlinings, innermost last
    std::vector<FuncLoopTarget> m_funcLoopTargets;
    std::set<const SubprogramDecl*> m_inlineBodies;  // Recursion guard of procedures
    std::map<const SubprogramDecl*, int> m_functionDepth;  // Active inlinings of a function
    int m_recursiveCalls = 0;  // Recursive inlinings under the outermost call
    //! Cost of the element writes folded into one inlined body; each rebuilds the local value
    int64_t m_funcWriteCost = 0;
    std::map<std::string, std::vector<SubprogramRef>> m_subprograms;  // Visible, by name
    std::map<std::string, const ComponentDecl*> m_components;  // Visible, by name
    int m_concurrentProc = 0;  // Lowering a concurrent procedure call body
    bool m_passive = false;  // Lowering the entity statement part, which may not assign
    ScopeSave m_moduleScope;  // Module declarative part, hidden by processes
    int m_procVarSeq = 0;  // Disambiguates procedure local variables
    const VType* m_funcRetp = nullptr;  // Return-type context for function bodies
    CowMap<VarInfo> m_scope;  // Signals and ports by folded name; copy-on-write
    //! Statements produced while lowering an expression, emitted before the statement
    AstNodeStmt* m_pendingStmtp = nullptr;
    //! Whether this point has a statement sink, so a local array can become a variable
    bool m_materializeLocals = false;
    int m_localVarSeq = 0;  // Distinguishes materialized locals of separate calls
    //! Non-zero while the evaluator lowers several continuations of one statement
    int m_runtimeBranchDepth = 0;
    int m_unusedDepth = 0;  // For a scope that has to exist unconditionally
    struct RuntimeBranchScope final {
        int& depth;  // Counter incremented while this scope is alive
        explicit RuntimeBranchScope(int& d)
            : depth(d) {
            ++depth;
        }
        ~RuntimeBranchScope() { --depth; }
    };
    void addPendingStmt(AstNodeStmt* nodep) {
        m_pendingStmtp = m_pendingStmtp
                             ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(m_pendingStmtp, nodep)
                             : nodep;
    }
    AstNodeStmt* takePendingStmts() {
        AstNodeStmt* const pendingp = m_pendingStmtp;
        m_pendingStmtp = nullptr;
        return pendingp;
    }
    //! Prepend the statements an inlined call produced to `bodyp`
    AstNodeStmt* takePendingFor(AstNodeStmt* bodyp) {
        AstNodeStmt* const pendingp = takePendingStmts();
        if (!pendingp) return bodyp;
        return bodyp ? AstNode::addNext<AstNodeStmt, AstNodeStmt>(pendingp, bodyp) : pendingp;
    }
    void discardPendingStmts() {
        if (m_pendingStmtp) m_pendingStmtp->deleteTree();
        m_pendingStmtp = nullptr;
    }
    // Symbolic values of process variables, for slice widths from their relations
    std::map<std::string, ExprP> m_varVals;
    std::map<std::string, int64_t> m_consts;  // Integer constants by folded name
    //! Integer constants needed while registering types, before full declaration emission
    std::map<std::string, int64_t> m_preConsts;
    //! Elaboration-time real constants by folded name
    std::map<std::string, double> m_realConsts;
    //! A package specification constant whose initializer calls a subprogram with no body
    //! yet: the body's pass completes it
    struct DeferredConst final {
        // Declared (folded, spelling) names
        std::vector<std::pair<std::string, std::string>> names;
        const TypeSpec* typep = nullptr;  // Declared subtype indication
        const Expr* initp = nullptr;  // Initial value expression
        FileLine* flp = nullptr;  // Source location
        std::string owner;  // Unit that declared it: only that unit completes it
    };
    // Package constants whose initialization is deferred
    std::vector<DeferredConst> m_deferredConsts;
    std::string m_unitName;  // Unit being emitted, which owns its deferred constants
    //! A pure function call the elaboration-time folders are evaluating: its formals
    //! bound to the values of the actuals, plus the declaration to stop a body folding
    //! into itself. Only the innermost frame is in scope and every actual is already
    //! valued, so an evaluation over a body cannot re-enter itself. Pushed and popped
    //! around one evaluation, so this is empty outside a fold.
    struct PureCall final {
        std::map<std::string, ExprP> args;  // Formal values bound to the actuals, by folded name
        const SubprogramDecl* subp = nullptr;  // Subprogram being folded
    };
    mutable std::vector<PureCall> m_pureCalls;  // Pure calls being folded, innermost last
    std::map<std::string, VType> m_types;  // Declared types by folded name
    //! A type the declarative part deferred, with the sequence number of the part that
    //! deferred it: `resolveType` registers one at its first use, once the emitter has
    //! computed the constant its constraint names, and only its own part may finish it
    struct DeferredType final {
        const TypeDecl* declp;  // Type declaration that was deferred
        size_t seq;  // Sequence number of the part that deferred it
    };
    std::map<std::string, DeferredType> m_deferredTypes;  // Deferred types by folded name
    size_t m_deferSeq = 0;  // Sequence number of the part being registered
    std::map<std::string, AstConst*> m_constValues;  // Typed constants by folded name
    std::map<std::string, VType> m_constTypes;  // Type of each typed constant
    //! The generic bindings of the instance whose port map lowers, so a port default
    //! that names the entity's own generics sees the values this instance binds
    GenericBindings m_instanceBindings;
    std::map<std::string, std::string> m_stringConsts;  // Folded name to its text
    //! One value inside a subprogram that reads a file at elaboration
    struct FileVal final {
        int64_t i = 0;  // INTEGER value, or a line's read cursor
        int width = 0;  // Width of a vector value
        std::string bits;  // Vector value, most significant bit first
        std::string text;  // STRING or LINE value
        std::map<size_t, std::string> elems;  // Image elements by index
    };
    struct FileReader final {
        std::ifstream in;  // Stream reading the file
        bool eof = false;  // End of file reached
    };
    using FileVals = std::map<std::string, FileVal>;
    using FileReaders = std::map<std::string, FileReader>;
    AstNodeExpr* evalFileInit(const SubprogramDecl& subp, const Expr& call);
    bool evalFileExpr(const Expr& expr, FileVal& out, FileVals& vals, FileReaders& files);
    bool evalFileStmts(const std::vector<StmtP>& stmts, FileVal& result, bool& returned,
                       bool& exited, FileVals& vals, FileReaders& files);
    bool fileAssign(const Stmt& stmt, FileVal& value, FileVals& vals, FileReaders& files);
    bool fileCall(const Stmt& stmt, FileVals& vals, FileReaders& files);
    std::map<std::string, AstConst*> m_enumConsts;  // Enumeration literals by folded name
    std::vector<std::unique_ptr<AstConst>> m_ownedConsts;  // Owns constants in the tables
    struct Edge final {
        AstVar* varp = nullptr;  // Signal whose edge is tested
        VEdgeType type = VEdgeType::ET_CHANGED;  // Edge kind tested on varp
    };
    std::vector<Edge> m_edges;  // Clock edges collected while lowering a process
    bool m_initial = false;  // Emitting the time-zero evaluation
    //! The process has one sensitivity signal, so it runs on every event of that signal
    //! and an edge call is that signal's level rather than a constant
    bool m_edgeAsLevel = false;
    //! The process has several sensitivity signals, so an edge call must compare the
    //! signal with its value at the previous run of the process
    bool m_edgeAsPrev = false;
    //! Order previous-value variables by source position, then name, for stable emission
    struct AstVarLess final {
        bool operator()(const AstVar* const ap, const AstVar* const bp) const {
            const int cmp = ap->fileline()->operatorCompare(*bp->fileline());
            if (cmp != 0) return cmp < 0;
            if (ap->name() != bp->name()) return ap->name() < bp->name();
            return std::less<const AstVar*>{}(ap, bp);
        }
    };
    std::map<AstVar*, AstVar*, AstVarLess> m_edgePrevVars;  // Edge signal -> its previous value
    int m_edgeSeq = 0;  // Disambiguates the previous-value variables
    //! Set while a wait process lowers its body: a bare `wait;` reached at run time
    //! sets this flag, which gates every later activation
    AstVar* m_suspendFlagp = nullptr;
    int m_varSeq = 0;  // Disambiguates a process's own variables module-wide
    //! Active loop, with the flags its exit/next conditions latch
    struct LoopFrame final {
        AstVar* exitp = nullptr;  // Set when an exit condition fired
        AstVar* nextp = nullptr;  // Set when a next condition fired
        std::string label;  // Loop label, empty when unlabeled
        bool guard = false;  // The body uses exit/next, so statements need guarding
    };
    std::vector<LoopFrame> m_loops;  // Active loops, innermost last
    int m_loopSeq = 0;  // Disambiguates the loop flag variables
    int m_genSeq = 0;  // Disambiguates sibling generate bodies
    std::string m_genSuffix;  // Suffix for names created inside a generate
    const ModuleMap* m_modulesp = nullptr;  // Modules of the instantiated entities
    const PackageTable::Impl* m_packagesp = nullptr;  // Packages visible to this unit
    std::set<std::string> m_imported;  // Names that came from a used package
    std::set<std::string> m_ambiguousTypes;  // Type names imported from two packages
    std::set<std::string> m_ambiguousLiterals;  // Enumeration literals from two types
    bool m_stdLogicSigned = false;  // `use ieee.std_logic_signed.all` is in effect
    bool m_stdLogicUnsigned = false;  // `use ieee.std_logic_unsigned.all` is in effect
    bool m_stdLogicArith = false;  // `use ieee.std_logic_arith.all` is in effect
    bool m_numericStdUnsigned = false;  // `use ieee.numeric_std_unsigned.all`
    bool m_numericStdSigned = false;  // `use ieee.numeric_std_signed.all`
    //! Standard packages the unit's `use` clauses name, as `pkg` for `.all` or `pkg.decl`
    std::set<std::string> m_stdUses;
    //! Element structure of an array dtype, including a function result's return type
    std::map<AstNodeDType*, VType> m_arrayDtypes;
    //! The array structure of one lowering of an expression, which its packed dtype loses
    struct LoweredType final {
        const AstNodeExpr* nodep;  // The lowered expression the type belongs to
        VType type;  // Its array structure
    };
    //! Latest lowering of each aggregate or concatenation; an earlier one does not match
    std::map<const Expr*, LoweredType> m_exprTypes;
    std::set<std::string> m_variables;  // Module-level variables, which are not signals
    //! Signals whose type has no resolution function: at most one driver (IEEE 1076-2008
    //! 6.4.2.3), unlike std_logic and the arrays built from it
    std::set<AstVar*> m_unresolvedSignals;
    std::map<std::string, AliasInfo> m_aliases;  // Aliases visible in the unit, by folded name
    mutable std::set<std::string> m_aliasBusy;  // Aliases being expanded (recursion guard)
    Design* const m_designp;  // Design context for generic specialization

    //! Trace a declared object, as the SystemVerilog grammar does for its variables
    static void traceDeclared(AstVar* varp) {
        varp->trace(v3Global.opt.trace() && varp->fileline()->tracingOn());
    }
    AstNodeDType* findBasic(FileLine* flp, VBasicDTypeKwd kwd) const {
        return m_rootp->typeTablep()->findBasicDType(flp, kwd);
    }
    AstNodeDType* findVector(FileLine* flp, VBasicDTypeKwd kwd, VSigning signing, int left,
                             int right, bool ascending) const {
        const int width = std::abs(left - right) + 1;
        if (signing == VSigning::NOSIGN) {
            // AstBasicDType normalizes NOSIGN to UNSIGNED for a logic keyword, which would
            // make a VHDL array indistinguishable from numeric_std unsigned. Keep a
            // separate dtype instance so the frontend can tell the two apart.
            AstBasicDType* const dtp = new AstBasicDType{flp, kwd, VSigning::UNSIGNED,
                                                         VNumRange{left, right, ascending}, width};
            dtp->numeric(VSigning::NOSIGN);
            m_rootp->typeTablep()->addTypesp(dtp);
            return dtp;
        }
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
        type.typeName = "integer";
        return type;
    }

    //! The predefined integer types, modeled directly rather than as a declared entry in
    //! m_types, so a discrete attribute of one needs their bounds here. All three ascend
    //! from -2^31, 0 and 1 to the high bound of the 32-bit signed model of intType
    static bool isPredefinedIntegerMark(const std::string& mark) {
        return mark == "integer" || mark == "natural" || mark == "positive";
    }
    bool predefinedIntegerType(FileLine* flp, const std::string& mark, VType& out) const {
        if (!isPredefinedIntegerMark(mark)) return false;
        out = intType(flp);
        out.left = mark == "integer" ? -0x80000000 : (mark == "natural" ? 0 : 1);
        out.right = 0x7fffffff;
        out.ascending = true;
        return true;
    }
    VType timeType(FileLine* flp) const {
        // A physical type has no modelled delay here, so it is a 64-bit count of the step
        // unit (femtoseconds for 'time'); 64 bits are needed for values like 2 sec = 2e15 fs
        VType type;
        type.kind = VType::INT;
        type.width = 64;
        type.kwd = VBasicDTypeKwd::LONGINT;
        type.dtp = findBasic(flp, VBasicDTypeKwd::LONGINT);
        type.typeName = "time";
        return type;
    }
    VType logicType(FileLine* flp) const {
        VType type;
        type.kind = VType::LOGIC;
        type.dtp = findBasic(flp, VBasicDTypeKwd::LOGIC);
        type.typeName = "std_ulogic";
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
        if (basicp->keyword() == VBasicDTypeKwd::INT
            || basicp->keyword() == VBasicDTypeKwd::LONGINT) {
            type.kind = VType::INT;
            type.typeName = "integer";
        } else if (basicp->isRanged()) {
            // A ranged width-1 dtype is a one-element vector, not a scalar
            type.kind = VType::VECTOR;
            type.left = basicp->left();
            type.right = basicp->right();
            type.ascending = basicp->ascending();
            type.signing = basicp->isNosign()
                               ? VSigning::NOSIGN
                               : (dtp->isSigned() ? VSigning::SIGNED : VSigning::UNSIGNED);
            if (basicp->keyword() == VBasicDTypeKwd::BIT) {
                type.typeName = "bit_vector";
            } else if (type.signing == VSigning::SIGNED) {
                type.typeName = "signed";
            } else if (type.signing == VSigning::UNSIGNED) {
                type.typeName = "unsigned";
            }
        } else {
            type.kind = basicp->keyword() == VBasicDTypeKwd::BIT ? VType::BIT : VType::LOGIC;
            type.typeName = type.kind == VType::BIT ? "bit" : "std_ulogic";
        }
        return type;
    }
    //! The enumeration type behind a dtype, with its literal order, if it has one
    static bool enumTypeOfDType(AstNodeDType* dtp, VType& out) {
        AstEnumDType* const enump = VN_CAST(dtp->skipRefToEnump(), EnumDType);
        if (!enump) return false;
        out = VType{};
        out.kind = VType::ENUM;
        out.dtp = dtp;
        out.width = dtp->width();
        for (AstEnumItem* itemp = enump->itemsp(); itemp;
             itemp = VN_CAST(itemp->nextp(), EnumItem)) {
            out.literals.push_back(itemp->name());
        }
        return true;
    }
    //! Type of a lowered operand. A designator keeps the declared array structure,
    //! which a packed dtype does not carry.
    VType typeOfLowered(const Expr& expr, AstNodeExpr* nodep) const {
        UASSERT_OBJ(nodep->dtypep(), nodep, "VHDL expression has no data type");
        VType type;
        if (typeOfDesignator(expr, type) && type.width == nodep->width()) return type;
        const Expr* structp = &expr;
        while (structp->kind == Expr::PAREN && structp->a) structp = structp->a.get();
        const auto exprIt = m_exprTypes.find(structp);
        if (exprIt != m_exprTypes.end() && exprIt->second.nodep == nodep) {
            return exprIt->second.type;
        }
        // An enumeration value has the ENUM type its declared objects have
        if (enumTypeOfDType(nodep->dtypep(), type)) return type;
        const auto arrayIt = m_arrayDtypes.find(nodep->dtypep());
        if (arrayIt != m_arrayDtypes.end() && arrayIt->second.width == nodep->width()) {
            return arrayIt->second;
        }
        return typeOfDType(nodep->dtypep());
    }

    //! Whether a value of this type is an aggregate, which never folds as an integer
    static bool isAggregateType(const VType& type) {
        return type.kind == VType::ARRAY || type.kind == VType::RECORD;
    }

    AstConst* makeIntConst(FileLine* flp, int64_t value) const {
        const VType type = intType(flp);
        V3Number num{flp, type.width, static_cast<uint32_t>(value)};
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(type.dtp);
        return constp;
    }
    AstConst* makeTimeConst(FileLine* flp, int64_t value) const {
        const VType type = timeType(flp);
        V3Number num{flp, type.width, static_cast<uint32_t>(value)};
        num.setQuad(static_cast<uint64_t>(value));
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(type.dtp);
        return constp;
    }
    //! Constant from a logic bit string; '1' sets, 'x'/'-' are don't-care, 'z' resolves low
    AstConst* makeBitsConst(FileLine* flp, AstNodeDType* dtp, const std::string& bits) const {
        const int width = static_cast<int>(bits.size());
        V3Number num{flp, width, 0u};
        for (int i = 0; i < width; ++i) {
            const char bit = bits[width - 1 - i];  // bits[0] is the most significant bit
            if (bit == '1') {
                num.setBit(i, '1');
            } else if (bit == 'x' || bit == '-') {
                num.setBit(i, 'x');
            } else if (bit == 'z') {
                // Keep the high-impedance value: V3Tristate lowers it for a tristate
                // driver, and any other destination resolves it to 0 (as a real 'Z' does)
                num.setBit(i, 'z');
            }
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
    //! Constant from a character string: an unsigned vector of 8-bit codes with the
    //! first character in the most significant byte, matching a string generic's layout
    AstConst* makeStringConst(FileLine* flp, const std::string& text) const {
        const int length = static_cast<int>(text.size());
        const int width = 8 * length;
        AstNodeDType* const dtp
            = findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, width);
        V3Number num{flp, width, 0u};
        for (int i = 0; i < length; ++i) {
            const unsigned char code = static_cast<unsigned char>(text[static_cast<size_t>(i)]);
            const int base = (length - 1 - i) * 8;
            for (int bit = 0; bit < 8; ++bit) {
                if ((code >> bit) & 1U) num.setBit(base + bit, '1');
            }
        }
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(dtp);
        return constp;
    }
    AstNodeExpr* bitConst(FileLine* flp, char bit) const {
        return makeBitsConst(flp, findBasic(flp, VBasicDTypeKwd::LOGIC), std::string(1, bit));
    }
    //! A std_logic_vector that std_logic_signed makes a two's complement number
    bool isSignedLogicVector(const VType& type) const {
        return m_stdLogicSigned && !m_stdLogicUnsigned && type.isPredefinedVector()
               && type.kwd == VBasicDTypeKwd::LOGIC;
    }
    //! Like resizeExpr, but widening copies the sign bit whatever the operand's type
    AstNodeExpr* signExtendExpr(AstNodeExpr* exprp, const VType& to, FileLine* flp) const {
        if (exprp->width() >= to.width) return resizeExpr(exprp, to, flp);
        AstNodeExpr* const resultp = new AstExtendS{flp, exprp, to.width};
        resultp->dtypep(to.dtp);
        return resultp;
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
    bool resolveTypePredefined(const TypeSpec& spec, const std::string& mark, VType& out);
    //! Whether a use clause or a qualified name makes a type of a standard package visible
    bool checkStdTypeVisible(const TypeSpec& spec, const std::string& mark);
    void registerType(const TypeDecl& decl);
    void registerEnumType(FileLine* flp, const TypeDecl& decl, VType& type);
    bool resolveArrayElement(const TypeDecl& decl, VType& elem);
    bool registerArrayType(const TypeDecl& decl, VType& type);
    bool registerRecordType(const TypeDecl& decl, VType& type);
    void emitPackage(const PackageUnit& pkg);
    //! Register a declarative part's types, deferring one whose constraint names a constant
    //! of the same part written above it that the pre-scan did not fold: the part's
    //! constants are emitted after all of its types, so only a first use can see the value
    void registerTypes(const std::vector<TypeDecl>& types, const std::vector<Decl>& decls);
    //! Register the deferred types, reporting one whose constraint still cannot resolve
    void finishDeferredTypes();
    static bool exprNamesAny(const Expr& expr, const std::set<std::string>& names);
    static bool typeDeclNamesAny(const TypeDecl& decl, const std::set<std::string>& names);
    static bool typeDeclMarksAny(const TypeDecl& decl, const std::set<std::string>& names);
    //! Whether a range attribute of the type names an object the part has not declared yet
    bool typeDeclNeedsLaterObject(const TypeDecl& decl) const;
    bool exprNeedsLaterObject(const Expr& expr) const;
    //! Make a package visible; `only` names one declaration to import instead
    void importPackage(const std::string& name, const std::string& only = "");
    void importInlineScope(const std::string& name);
    //! Record integer constants that a type declaration may depend on
    void preScanIntConstants(const std::vector<Decl>& decls);
    void emitConstant(const std::vector<std::pair<std::string, std::string>>& names,
                      const TypeSpec& type, const Expr* initp, FileLine* flp, bool deferredOk);
    //! Resolved type of a constant, with a value lowered early when the type needed it
    struct ConstantTypeInfo final {
        VType type;  // Resolved type of the constant
        bool intConstant = true;  // The constant has an integer type
        bool realConstant = false;  // The constant has type real
        AstNodeExpr* valuep = nullptr;  // A value lowered early, when the type needed it
    };
    bool deferUnresolvedConstant(const std::vector<std::pair<std::string, std::string>>& names,
                                 const TypeSpec& type, const Expr& init, FileLine* flp);
    bool resolveConstantType(const TypeSpec& type, const Expr& init, FileLine* flp,
                             ConstantTypeInfo& info);
    bool storeIntConstant(const std::vector<std::pair<std::string, std::string>>& names,
                          const TypeSpec& type, const Expr& init, FileLine* flp,
                          const VType& constType);
    bool registerGenerics(const std::vector<GenericDecl>& generics,
                          const GenericBindings* bindingsp, bool probe);
    void freeInstanceBindings();  // Releases the bindings kept for one instance's ports
    AstModule* specializeInstance(const Concurrent& conc, const EntityUnit& entity,
                                  AstModule* basep, const PortBoundsMap& portBounds);
    //! Whether a port's index range comes from the instance rather than the entity
    static bool unconstrainedPort(const PortDecl& port) {
        const std::string& mark = port.type.mark;
        const bool unconstrainedMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                                       || mark == "bit_vector" || mark == "unsigned"
                                       || mark == "signed";
        return unconstrainedMark && !port.type.hasRange && port.type.constraintMark.empty();
    }
    //! Derive an unconstrained port's range from its associations at one instance
    bool derivePortBounds(const EntityUnit& entity, const Concurrent& conc, const PortDecl& port,
                          PortBounds& out);
    //! Derive an unconstrained record port's element ranges from its associations
    bool deriveRecordBounds(const EntityUnit& entity, const Concurrent& conc, const PortDecl& port,
                            PortBounds& out);
    //! The record type a port declares, from the parent's scope or the child's packages
    bool resolvePortRecordType(const EntityUnit& entity, const PortDecl& port, VType& out) const;
    //! Resolve a generic type association such as `std_logic_vector(3 downto 0)`
    bool resolveTypeActual(const Expr& actual, VType& out);
    bool reserveName(const std::string& folded, const std::string& spelling, FileLine* flp);
    AstNodeDType* enumDType(FileLine* flp, const TypeDecl& decl, int width);
    bool applyRange(const TypeSpec& spec, VType& type);
    //! Constrain a nested-array subtype `t(r1)(r2, ...)` by descending one level per
    //! constraint; a fresh copy, and any residual unconstrained level is rejected
    bool applyIndexConstraints(const TypeSpec& spec, VType& out);
    //! Apply a record subtype's per-member ranges, widening each constrained element
    bool applyMemberConstraints(const TypeSpec& spec, VType& out);
    //! Complete a record type with unconstrained members from a constant's aggregate
    void completeRecordType(TypeSpec& spec, const Expr& init);
    //! Deep copy of a subtype indication, for a constraint completed from a value
    static TypeSpec copySpec(const TypeSpec& spec);
    //! Constrain one record element to a fixed vector range
    void setRecordField(FileLine* flp, VField& field, int left, int right, bool ascending);
    //! Recompute a record's total width, MSB-first offsets and packed dtype
    void finalizeRecordFields(FileLine* flp, VType& rec);
    bool constraintBounds(const TypeSpec& spec, int64_t& left, int64_t& right, bool& ascending);
    //! The index range an array attribute names: `A'HIGH(2)` is the second one, and an
    //! attribute written without an index is the first, IEEE 1076-2008 16.2.3
    const VType* indexRangeLevel(const Expr& expr, const VType& type) const;
    bool evalInt(const Expr& expr, int64_t& value) const;
    bool evalIntName(const Expr& expr, int64_t& value) const;
    bool evalIntAttribute(const Expr& expr, int64_t& value) const;
    bool evalIntBinary(const Expr& expr, int64_t& value) const;
    bool evalIntIndex(const Expr& expr, int64_t& value) const;
    //! Text of a string operand whose characters are fixed at elaboration
    bool stringOperandText(const Expr& expr, std::string& out) const;
    //! Evaluate a locally static real-valued expression
    bool evalReal(const Expr& expr, double& value) const;
    //! Evaluate a locally static condition: a literal, a comparison of literals, or a
    //! pure function of literals returning one. An elaboration-time check such as
    //! `assert is_power_of_two(n)` folds, so its callee's body is never lowered.
    bool evalCond(const Expr& expr, bool& value) const;
    bool mentionsReal(const Expr& expr) const;
    bool rejectUnmodelledObject(const VType& type, FileLine* flp, bool allowUnconstrained = false);
    bool staticInt(const ExprP& exprp, int64_t& value) const {
        return exprp && evalInt(*exprp, value);
    }
    //! Static integer value, also folding a call to a pure function
    bool staticIntFold(const ExprP& exprp, int64_t& value);
    //! Whether a delay expression is a static zero, which a cycle model already has
    bool staticZeroDelay(const ExprP& delayp) {
        int64_t value = 0;
        return delayp && staticIntFold(delayp, value) && value == 0;
    }
    bool pushPureCall(const Expr& call, const Expr*& returnp) const;
    //! Whether every name a pure body's return expression holds is a literal, a formal or
    //! a type mark, so the value is closed over the call
    bool closedOverFormals(const Expr& expr, const std::map<std::string, ExprP>& args) const;
    void popPureCall() const;
    //! Deep copy of a frontend expression, for a synthesized type constraint
    static ExprP cloneExpr(const Expr& expr) {
        ExprP out = std::make_unique<Expr>(expr.kind);
        out->flp = expr.flp;
        out->text = expr.text;
        out->spelling = expr.spelling;
        out->intValue = expr.intValue;
        out->charValue = expr.charValue;
        out->bits = expr.bits;
        out->formalNames = expr.formalNames;
        out->a = expr.a ? cloneExpr(*expr.a) : ExprP{};
        out->b = expr.b ? cloneExpr(*expr.b) : ExprP{};
        out->c = expr.c ? cloneExpr(*expr.c) : ExprP{};
        for (const ExprP& argp : expr.args) out->args.push_back(argp ? cloneExpr(*argp) : ExprP{});
        for (const std::vector<ExprP>& choiceList : expr.choices) {
            std::vector<ExprP> copy;
            for (const ExprP& choicep : choiceList) {
                copy.push_back(choicep ? cloneExpr(*choicep) : ExprP{});
            }
            out->choices.push_back(std::move(copy));
        }
        return out;
    }
    //! Whether two expressions are structurally the same
    static bool sameExpr(const Expr& l, const Expr& r) {
        if (l.kind != r.kind || l.text != r.text || l.intValue != r.intValue
            || l.charValue != r.charValue || l.bits != r.bits || l.args.size() != r.args.size()
            || l.choices.size() != r.choices.size()) {
            return false;
        }
        for (size_t i = 0; i < l.args.size(); ++i) {
            if (static_cast<bool>(l.args[i]) != static_cast<bool>(r.args[i])) return false;
            if (l.args[i] && !sameExpr(*l.args[i], *r.args[i])) return false;
        }
        for (size_t i = 0; i < l.choices.size(); ++i) {
            if (l.choices[i].size() != r.choices[i].size()) return false;
            for (size_t j = 0; j < l.choices[i].size(); ++j) {
                if (static_cast<bool>(l.choices[i][j]) != static_cast<bool>(r.choices[i][j])) {
                    return false;
                }
                if (l.choices[i][j] && !sameExpr(*l.choices[i][j], *r.choices[i][j])) {
                    return false;
                }
            }
        }
        const auto sameChild = [](const ExprP& lc, const ExprP& rc) {
            return static_cast<bool>(lc) == static_cast<bool>(rc) && (!lc || sameExpr(*lc, *rc));
        };
        return sameChild(l.a, r.a) && sameChild(l.b, r.b) && sameChild(l.c, r.c);
    }
    //! Add or subtract two linear forms (coef * atom + const); atoms must match to keep
    //! a single dynamic term, otherwise the sum is not linear in one unknown
    static bool combineLinear(const Expr* atomAp, int64_t coefA, int64_t conA, const Expr* atomBp,
                              int64_t coefB, int64_t conB, int64_t sign, const Expr*& atomOut,
                              int64_t& coefOut, int64_t& conOut) {
        const int64_t bCoef = coefB * sign;
        const int64_t bCon = conB * sign;
        if (coefA == 0) {
            atomOut = atomBp;
            coefOut = bCoef;
            conOut = conA + bCon;
            return true;
        }
        if (bCoef == 0) {
            atomOut = atomAp;
            coefOut = coefA;
            conOut = conA + bCon;
            return true;
        }
        if (!sameExpr(*atomAp, *atomBp)) return false;  // two distinct dynamic terms
        atomOut = atomAp;
        coefOut = coefA + bCoef;
        conOut = conA + bCon;
        return true;
    }
    //! Parse an expression into coef * atom + const: fold constants, add/subtract like
    //! terms and scale by a constant multiplicand; atomOut is null for a pure constant
    bool linearForm(const Expr& expr, const Expr*& atomOut, int64_t& coefOut,
                    int64_t& conOut) const {
        const Expr* ep = &expr;
        while (ep->kind == Expr::PAREN && ep->a) ep = ep->a.get();
        atomOut = nullptr;
        coefOut = 0;
        conOut = 0;
        int64_t value = 0;
        if (evalInt(*ep, value)) {
            conOut = value;
            return true;
        }
        if (ep->kind == Expr::BINARY && ep->a && ep->b) {
            const Expr* aAtomp = nullptr;
            const Expr* bAtomp = nullptr;
            int64_t aCoef = 0;
            int64_t aCon = 0;
            int64_t bCoef = 0;
            int64_t bCon = 0;
            const int sign = ep->text == "+" ? 1 : ep->text == "-" ? -1 : 0;
            if (sign != 0 && linearForm(*ep->a, aAtomp, aCoef, aCon)
                && linearForm(*ep->b, bAtomp, bCoef, bCon)) {
                return combineLinear(aAtomp, aCoef, aCon, bAtomp, bCoef, bCon, sign, atomOut,
                                     coefOut, conOut);
            }
            if (ep->text == "*") {
                if (evalInt(*ep->b, value) && linearForm(*ep->a, aAtomp, aCoef, aCon)) {
                    atomOut = aAtomp;
                    coefOut = aCoef * value;
                    conOut = aCon * value;
                    return true;
                }
                if (evalInt(*ep->a, value) && linearForm(*ep->b, bAtomp, bCoef, bCon)) {
                    atomOut = bAtomp;
                    coefOut = bCoef * value;
                    conOut = bCon * value;
                    return true;
                }
            }
        }
        // An opaque dynamic term: 1 * e + 0
        atomOut = ep;
        coefOut = 1;
        conOut = 0;
        return true;
    }
    //! Clone an expression, replacing variables with their tracked values
    ExprP substVars(const Expr& expr, int depth = 0) const {
        ExprP copy = std::make_unique<Expr>(expr.kind);
        copy->flp = expr.flp;
        copy->text = expr.text;
        copy->spelling = expr.spelling;
        copy->intValue = expr.intValue;
        copy->charValue = expr.charValue;
        copy->bits = expr.bits;
        copy->formalNames = expr.formalNames;
        // A type attribute reads the declared type, not the tracked value, so its prefix
        // keeps its name: `v(v'high downto 1)` after `v := f(x)` still names `v`
        const bool staticAttr
            = expr.kind == Expr::ATTR
              && (expr.text == "range" || expr.text == "reverse_range" || expr.text == "high"
                  || expr.text == "low" || expr.text == "left" || expr.text == "right"
                  || expr.text == "length" || expr.text == "ascending");
        if (expr.a) copy->a = staticAttr ? cloneExpr(*expr.a) : substVars(*expr.a, depth + 1);
        if (expr.b) copy->b = substVars(*expr.b, depth + 1);
        if (expr.c) copy->c = substVars(*expr.c, depth + 1);
        for (const ExprP& argp : expr.args) {
            copy->args.push_back(argp ? substVars(*argp, depth + 1) : nullptr);
        }
        for (const auto& choiceList : expr.choices) {
            copy->choices.emplace_back();
            for (const ExprP& choicep : choiceList) {
                copy->choices.back().push_back(choicep ? substVars(*choicep, depth + 1) : nullptr);
            }
        }
        if (expr.kind == Expr::NAME && depth < 8 && !activeEnv().count(expr.text)) {
            const auto it = m_varVals.find(expr.text);
            if (it != m_varVals.end()) return substVars(*it->second, depth + 1);
        }
        return copy;
    }
    //! Copy the tracked variable values, for a sibling branch to start from the same ones
    std::map<std::string, ExprP> saveVarVals() const {
        std::map<std::string, ExprP> out;
        for (const auto& entry : m_varVals) out[entry.first] = substVars(*entry.second);
        return out;
    }
    void restoreVarVals(const std::map<std::string, ExprP>& saved) {
        m_varVals.clear();
        for (const auto& entry : saved) m_varVals[entry.first] = substVars(*entry.second);
    }
    //! Record or invalidate a variable's tracked value after a statement
    void updateVarVals(const Stmt& stmt) {
        if (stmt.kind == Stmt::VAR_ASSIGN) {
            const Expr* basep = stmt.target.get();
            while (basep
                   && (basep->kind == Expr::INDEX || basep->kind == Expr::SLICE
                       || basep->kind == Expr::SELECTED)) {
                basep = basep->a.get();
            }
            if (basep && basep->kind == Expr::NAME) {
                if (stmt.target->kind == Expr::NAME && stmt.value) {
                    m_varVals[basep->text] = substVars(*stmt.value);
                } else {
                    m_varVals.erase(basep->text);  // Partial or conditional write: unknown
                }
            }
            return;
        }
        // A control statement or call may change any variable
        if (stmt.kind == Stmt::IF || stmt.kind == Stmt::CASE || stmt.kind == Stmt::FOR
            || stmt.kind == Stmt::WHILE || stmt.kind == Stmt::LOOP || stmt.kind == Stmt::CALL) {
            m_varVals.clear();
        }
    }
    //! Width of a slice whose bounds are not both static, when they are linear in the
    //! same dynamic term (e.g. `v(n * 8 - 1 downto (n - 1) * 8)`) so their difference is constant
    bool sliceWidth(const Expr& left, const Expr& right, bool ascending, int64_t& width) const {
        const ExprP leftCopy = substVars(left);
        const ExprP rightCopy = substVars(right);
        const Expr& hip = ascending ? *rightCopy : *leftCopy;
        const Expr& lop = ascending ? *leftCopy : *rightCopy;
        const Expr* hiAtomp = nullptr;
        const Expr* loAtomp = nullptr;
        int64_t hiCoef = 0;
        int64_t hiCon = 0;
        int64_t loCoef = 0;
        int64_t loCon = 0;
        if (!linearForm(hip, hiAtomp, hiCoef, hiCon) || !linearForm(lop, loAtomp, loCoef, loCon)) {
            return false;
        }
        if (hiCoef != loCoef || (hiCoef != 0 && !sameExpr(*hiAtomp, *loAtomp))) return false;
        width = hiCon - loCon + 1;
        return width > 0;
    }

    //! The variable a name denotes; an alias of a signal denotes its target, which is
    //! not in the scope under its own name
    AstVar* lookupVar(const Expr& nameExpr) {
        const auto it = m_scope.find(nameExpr.text);
        if (it != m_scope.end()) return it->second.varp;
        AstNodeExpr* const aliasp = lowerAlias(nameExpr, false);
        AstVarRef* const refp = VN_CAST(aliasp, VarRef);
        AstVar* const varp = refp ? refp->varp() : nullptr;
        if (aliasp) aliasp->deleteTree();
        return varp;
    }

    //! A vector mark that leaves the bounds open: the object's value fixes them
    static bool isUnconstrainedVectorMark(const TypeSpec& type) {
        if (type.hasRange || !type.constraintMark.empty()) return false;
        return type.mark == "std_logic_vector" || type.mark == "std_ulogic_vector"
               || type.mark == "bit_vector" || type.mark == "unsigned" || type.mark == "signed";
    }

    //! An unconstrained vector object whose value is a bit string: the literal fixes the
    //! width, e.g. `constant c : std_logic_vector := "0011"`
    bool isLiteralSizedVector(const TypeSpec& type, const Expr* initp) const {
        return initp && initp->kind == Expr::BITS_LIT && isUnconstrainedVectorMark(type);
    }

    //! The predefined functions that return the text of a value
    static bool isToStringFunction(const std::string& name) {
        return name == "to_string" || name == "to_bstring" || name == "to_ostring"
               || name == "to_hstring";
    }
    //! The type of a string of a given length: 8-bit character codes indexed from 1 up
    VType stringType(FileLine* flp, int length) const {
        VType type;
        type.kind = VType::ARRAY;
        type.elemp
            = std::make_shared<VType>(vectorType(flp, VBasicDTypeKwd::LOGIC, VSigning::NOSIGN, 8));
        type.elemWidth = 8;
        type.kwd = VBasicDTypeKwd::LOGIC;
        type.signing = VSigning::NOSIGN;
        type.left = 1;
        type.right = length;
        type.ascending = true;
        type.width = 8 * length;
        type.dtp = findVectorWidth(flp, VBasicDTypeKwd::LOGIC, VSigning::NOSIGN, type.width);
        type.typeName = "string";
        return type;
    }
    //! Signing a vector mark denotes: numeric_std unsigned/signed, or a VHDL array
    static VSigning vectorSigning(const std::string& mark) {
        if (mark == "signed" || mark == "u_signed") return VSigning::SIGNED;
        if (mark == "unsigned" || mark == "u_unsigned") return VSigning::UNSIGNED;
        return VSigning::NOSIGN;
    }
    //! The type such a value has: the mark's element type and the literal's width
    VType literalVectorType(FileLine* flp, const std::string& mark, int width) const {
        const VBasicDTypeKwd kwd
            = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
        VType type = vectorType(flp, kwd, vectorSigning(mark), width);
        type.typeName = mark == "u_signed" ? "signed" : (mark == "u_unsigned" ? "unsigned" : mark);
        return type;
    }

    //! Whether an expression contains a real literal, so its value is a real
    static bool hasRealLiteral(const Expr& expr) {
        if (expr.kind == Expr::REAL_LIT) return true;
        if (expr.a && hasRealLiteral(*expr.a)) return true;
        if (expr.b && hasRealLiteral(*expr.b)) return true;
        for (const ExprP& arg : expr.args) {
            if (arg && hasRealLiteral(*arg)) return true;
        }
        return false;
    }
    bool isIntrinsic(const std::string& name) const {
        static const char* const names[] = {"is_x",
                                            "is_x01",
                                            "is_x01z",
                                            "is_ux01",
                                            "to_x01",
                                            "to_01",
                                            "to_x01z",
                                            "to_ux01",
                                            "std_match",
                                            "resize",
                                            "shift_left",
                                            "shift_right",
                                            "shl",
                                            "shr",
                                            "rotate_left",
                                            "rotate_right",
                                            "minimum",
                                            "maximum",
                                            "find_leftmost",
                                            "find_rightmost",
                                            "and_reduce",
                                            "or_reduce",
                                            "xor_reduce",
                                            "nand_reduce",
                                            "nor_reduce",
                                            "xnor_reduce",
                                            "conv_integer",
                                            "conv_std_logic_vector",
                                            "conv_unsigned",
                                            "ext",
                                            "sxt",
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
    AstNodeExpr* lowerIntrinsicStdMatch(FileLine* flp, const std::vector<ExprP>& argsp);
    AstNodeExpr* lowerIntrinsicResize(FileLine* flp, const std::string& fn,
                                      const std::vector<ExprP>& argsp);
    //! Fixed-point types of ieee.fixed_pkg (V3VhdlNativeLowerFixed.cpp)
    static bool isFixedName(const std::string& name);
    static bool isFixedType(const VType& type) {
        return type.kind == VType::VECTOR
               && (type.typeName == "sfixed" || type.typeName == "ufixed");
    }
    bool fixedPackageUsed() const;
    bool isFixedFunction(const std::string& name) const;
    VType fixedType(FileLine* flp, bool isSigned, int left, int right) const;
    AstNodeExpr* noteFixed(const Expr& expr, AstNodeExpr* nodep, const VType& type);
    AstNodeExpr* fixedAlign(FileLine* flp, AstNodeExpr* valuep, const VType& type, int high,
                            int low);
    AstNodeExpr* fixedSel(FileLine* flp, AstNodeExpr* fromp, int lsb, int width);
    AstNodeExpr* fixedResize(FileLine* flp, AstNodeExpr* argp, const VType& at, int left,
                             int right, bool saturate, bool round);
    struct BinaryOperands;
    AstNodeExpr* lowerFixedBinary(FileLine* flp, const Expr& expr, const std::string& op,
                                  BinaryOperands& ops);
    AstNodeExpr* lowerFixedUnary(FileLine* flp, const Expr& expr, AstNodeExpr* operandp,
                                 const VType& type);
    //! `handled` is false when the call is not a fixed-point one (numeric_std's resize)
    AstNodeExpr* lowerFixedCall(const Expr& expr, bool& handled);
    AstNodeExpr* lowerFixedRealBinary(FileLine* flp, const Expr& expr, bool& handled);
    AstNodeExpr* fixedConstFromReal(FileLine* flp, double real, const VType& type, bool saturate,
                                    bool round);
    AstNodeExpr* lowerIntrinsicFind(FileLine* flp, const std::string& fn,
                                    const std::vector<ExprP>& argsp);
    AstNodeExpr* lowerIntrinsicMinMax(FileLine* flp, const std::string& fn,
                                      const std::vector<ExprP>& argsp);
    //! Unary reduction `and`/`or`/`xor` and their negations
    AstNodeExpr* makeReduction(FileLine* flp, AstNodeExpr* operandp, const std::string& op);
    AstNodeExpr* lowerEdgeCall(const Expr& expr);
    //! The clock's level, which is its edge when the process is change sensitive
    AstNodeExpr* edgeLevel(FileLine* flp, AstVar* varp, bool rising);
    //! Whether the signal differs from its value at the previous process run
    AstNodeExpr* edgeChanged(FileLine* flp, AstVar* varp);
    //! The signal's edge, from its level and whether it changed
    AstNodeExpr* edgeFromChanged(FileLine* flp, AstVar* varp, bool rising);
    AstNodeExpr* makeRotate(FileLine* flp, AstNodeExpr* valuep, AstNodeExpr* amountp, bool left);
    //! The shifted value for one direction, consuming its operands. `op` is one of
    //! sll/srl/sla/sra, already normalized to the direction the count names
    AstNodeExpr* makeShift(FileLine* flp, AstNodeExpr* valuep, AstNodeExpr* amountp,
                           const std::string& op, const VType& vt);
    AstNodeExpr* makeMod(FileLine* flp, AstNodeExpr* lp, AstNodeExpr* rp, AstNodeDType* dtp,
                         bool sign);
    AstNodeExpr* makeRem(FileLine* flp, AstNodeExpr* lp, AstNodeExpr* rp, AstNodeDType* dtp,
                         bool sign);
    bool isConversion(const std::string& name) const {
        return name == "std_logic_vector" || name == "std_ulogic_vector" || name == "bit_vector"
               || name == "unsigned" || name == "signed" || name == "u_unsigned"
               || name == "u_signed" || name == "to_integer" || name == "to_unsigned"
               || name == "to_signed" || name == "integer" || name == "natural"
               || name == "positive" || name == "time";
    }

    AstNodeExpr* lowerExpr(const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerCharLiteral(FileLine* flp, const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerBitStringLiteral(FileLine* flp, const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerName(FileLine* flp, const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerQualification(FileLine* flp, const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerUnary(FileLine* flp, const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerIndexedCall(FileLine* flp, const Expr& expr, const VType* contextp);
    ExprP indexOfNoFormalCall(const Expr& expr) const;
    AstNodeExpr* lowerToString(FileLine* flp, const Expr& expr, const std::string& name);
    AstNodeExpr* lowerOthersAssociation(FileLine* flp, const Expr& expr, const VType* contextp);
    //! Lowered operands of a binary expression with their resolved VHDL types
    struct BinaryOperands final {
        AstNodeExpr* lp = nullptr;  // Lowered left operand
        AstNodeExpr* rp = nullptr;  // Lowered right operand
        VType lt;  // Resolved type of the left operand
        VType rt;  // Resolved type of the right operand
    };
    //! Rejects a static integer result that overflows a 32-bit integer
    bool binaryStaticFits(FileLine* flp, const Expr& expr);
    AstNodeExpr* foldRealRelation(FileLine* flp, const Expr& expr, const std::string& op);
    bool isCharacterArrayOperand(const Expr& other);
    bool lowerBinaryOperands(const Expr& expr, const std::string& op, bool logicalOp,
                             BinaryOperands& ops);
    AstNodeExpr* lowerMatchingRelation(FileLine* flp, const Expr& expr, const std::string& op,
                                       BinaryOperands& ops);
    void reconcileRelationOperands(FileLine* flp, const Expr& expr, BinaryOperands& ops);
    void reSignBitStringLiteral(FileLine* flp, const Expr& side, AstNodeExpr*& operandp,
                                VType& opType, const VType& modelType);
    AstNodeExpr* lowerRelation(FileLine* flp, const Expr& expr, const std::string& cmpOp,
                               BinaryOperands& ops, AstNodeDType* bitDtp);
    AstNodeExpr* lowerConcat(FileLine* flp, const Expr& expr, BinaryOperands& ops);
    AstNodeExpr* lowerLogicalBinary(FileLine* flp, const std::string& op, BinaryOperands& ops);
    AstNodeExpr* lowerShiftBinary(FileLine* flp, const Expr& expr, const std::string& op,
                                  BinaryOperands& ops);
    AstNodeExpr* lowerPowerBinary(FileLine* flp, const Expr& expr, BinaryOperands& ops);
    AstNodeExpr* lowerIntArithmetic(FileLine* flp, const Expr& expr, const std::string& op,
                                    BinaryOperands& ops);
    AstNodeExpr* lowerVectorArithmetic(FileLine* flp, const Expr& expr, const std::string& op,
                                       BinaryOperands& ops);
    AstNodeExpr* lowerBinary(const Expr& expr, const VType* contextp);
    //! Lexicographic ordering of two VHDL arrays; consumes the operands
    AstNodeExpr* lowerArrayRelation(FileLine* flp, const std::string& cmpOp, const VType& lt,
                                    const VType& rt, AstNodeExpr*& lp, AstNodeExpr*& rp,
                                    AstNodeDType* bitDtp);
    AstNodeExpr* lowerConversion(const Expr& expr, const VType* contextp);
    AstNodeExpr* lowerDesignator(const Expr& expr, bool write);
    AstNodeExpr* lowerSel(const Expr& expr, AstNodeExpr* basep, const VType& baseType);
    AstNodeExpr* lowerRangeAttrSlice(FileLine* flp, const Expr& rangeExpr, AstNodeExpr* basep,
                                     const VType& baseType);
    AstNodeExpr* lowerIndexChain(const Expr& expr, AstNodeExpr* basep, const VType& baseType);
    AstNodeExpr* lowerDynamicSlice(const Expr& expr, AstNodeExpr* basep, const VType& baseType);
    bool sliceBound(const ExprP& expr, const std::string& prefix, const VType& baseType,
                    int64_t& out);
    AstNodeExpr* sliceOf(FileLine* flp, AstNodeExpr* basep, const VType& baseType, int elemWidth,
                         int hi, int lo, bool ascending);

    AstNodeStmt* lowerCase(const Stmt& stmt);
    //! Build an AstCase; a case over an enumeration is exhaustive
    AstCase* makeCase(FileLine* flp, AstNodeExpr* selectorp, AstCaseItem* itemsp) const {
        AstCase* const casep = new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
        if (VN_CAST(selectorp->dtypep()->skipRefToEnump(), EnumDType)) casep->uniquePragma(true);
        return casep;
    }
    void lowerWaitProcess(const Process& process);
    //! State shared by the helpers lowering a process that suspends on a wait
    struct WaitProcessLowering final {
        const Process* processp = nullptr;  // Process being lowered
        FileLine* flp = nullptr;  // Source location of the process
        std::size_t waitIndex = 0;  // Index of the wait statement, or the body size when none
        std::size_t stopIndex = 0;  // Index of the trailing bare wait, or the body size
        const Stmt* waitp = nullptr;  // The process's wait statement
        bool singleShot = false;  // A trailing bare wait: never resumes after the first run
        bool bareWaitInBody = false;  // A bare `wait;` occurs in the body
    };
    const Stmt* findProcessWait(const Process& process, std::size_t& waitIndex) const;
    void lowerWaitWithoutWait(const Process& process, FileLine* flp);
    bool findTrailingWait(const Process& process, WaitProcessLowering& ctx) const;
    void makeWaitSuspendFlag(FileLine* flp);
    void lowerTimedWaitProcess(const WaitProcessLowering& ctx);
    AstSenItem* makeWaitSensitivity(const Expr& waitValue, const std::vector<Edge>& edges,
                                    FileLine* flp) const;
    AstNodeStmt* makeWaitActivationBody(const WaitProcessLowering& ctx, AstNodeExpr* condp);
    ScopeSave saveScope() const;
    void restoreScope(const ScopeSave& saved);
    //! Restores the enclosing declaration scope on every exit path
    class ScopeGuard final {
        Lowering& m_lowering;  // Lowering whose scope tables are restored
        const ScopeSave m_saved;  // Scope tables captured on construction

    public:
        explicit ScopeGuard(Lowering& lowering)
            : m_lowering{lowering}
            , m_saved{lowering.saveScope()} {}
        ~ScopeGuard() { m_lowering.restoreScope(m_saved); }
    };
    void emitProcessDecls(const Process& process, std::set<std::string>& varNames);
    AstNodeStmt* lowerFor(const Stmt& stmt);
    AstNodeStmt* lowerForRuntime(const Stmt& stmt);
    AstNodeStmt* lowerReport(const Stmt& stmt);
    //! A failing assertion reports, and `severity failure` ends the run
    AstNodeStmt* lowerAssert(const Stmt& stmt);
    AstNodeStmt* assertBody(FileLine* flp, const Expr& condition, const Expr* messagep,
                            Stmt::Severity severity);
    AstNodeStmt* lowerDelay(const Expr& expr, FileLine* flp);
    static void collectReadNames(const std::vector<StmtP>& stmts, std::set<std::string>& names);
    bool stmtsContainEdge(const std::vector<StmtP>& stmts) const;
    static void collectExprNames(const Expr& expr, std::set<std::string>& names);
    //! The names an assignment target reads: an indexed name evaluates its index
    //! and a slice its bounds (IEEE 1076-2008 8.5); the target name is only written
    static void collectTargetReadNames(const Expr& expr, std::set<std::string>& names);
    static bool usesExitNext(const std::vector<StmtP>& stmts);
    static bool usesBareWait(const std::vector<StmtP>& stmts);
    static bool usesSeverityFailure(const std::vector<StmtP>& stmts);
    AstNodeExpr* loopGuard(FileLine* flp) const;
    AstNodeStmt* lowerWhile(const Stmt& stmt);
    AstVar* newLoopFlag(FileLine* flp, const char* what);
    LoopFrame* findLoop(const std::string& label);
    AstNodeStmt* clearLoopFlag(FileLine* flp, AstVar* flagp);
    AstNodeStmt* setLoopFlag(FileLine* flp, AstVar* flagp, AstNodeExpr* condp);
    AstNodeExpr* lowerCond(const Expr& expr);
    bool containsEdgeCall(const Expr& expr) const;
    AstNodeExpr* lowerMatching(const Expr& expr, AstNodeExpr* lp, AstNodeExpr* rp);
    //! Type information resolved for an attribute prefix
    struct AttrTypeInfo final {
        VType resolved;  // Type of the prefix when it names an object
        VType predefined;  // Predefined type when the prefix names one, e.g. integer
        const VType* typep = nullptr;  // Type the attribute applies to, null when unknown
        bool typeMark = false;  // The prefix names a type, not an object
    };
    bool lowerIndexedEventAttr(FileLine* flp, const Expr& expr, AstNodeExpr*& resultp);
    bool resolveAttrDesignator(FileLine* flp, const Expr& prefix, AttrTypeInfo& info);
    void resolveAttrNameType(FileLine* flp, const Expr& prefix, AttrTypeInfo& info);
    AstNodeExpr* lowerEventAttr(FileLine* flp, const Expr& expr);
    AstNodeExpr* lowerLogicMarkAttr(FileLine* flp, const Expr& expr, const std::string& attribute,
                                    const VType& type);
    AstNodeExpr* lowerCharacterAttr(FileLine* flp, const Expr& expr, const std::string& attribute,
                                    const VType& type);
    AstNodeExpr* lowerEnumAttr(FileLine* flp, const Expr& expr, const std::string& attribute,
                               const VType& type);
    AstNodeExpr* lowerAttrWithArgs(FileLine* flp, const Expr& expr, const std::string& attribute,
                                   const VType& type);
    AstNodeExpr* lowerRangeAttr(FileLine* flp, const std::string& attribute, const VType& type);
    AstNodeExpr* lowerAttr(const Expr& expr);
    //! One run of elements covered by one associated value
    struct AggregateSegment final {
        int offset = 0;  // Bit offset of the segment
        int width = 1;  // Bit width of the segment
        const Expr* exprp = nullptr;  // Value covering the segment
    };
    bool packedBitAggregateApplies(const Expr& expr, const VType& type);
    AstNodeExpr* lowerPackedBitAggregate(FileLine* flp, const Expr& expr, const VType& type);
    bool collectRecordAggregateSegments(FileLine* flp, const Expr& expr, const VType& type,
                                        std::vector<AggregateSegment>& segments,
                                        const Expr*& othersValuep);
    bool collectArrayAggregateSegments(FileLine* flp, const Expr& expr, const VType& type,
                                       std::vector<AggregateSegment>& segments,
                                       const Expr*& othersValuep);
    void fillAggregateOthers(const VType& type, std::vector<AggregateSegment>& segments,
                             const Expr* othersValuep);
    bool verifyAggregateCoverage(FileLine* flp, const VType& type,
                                 std::vector<AggregateSegment>& segments);
    AstNodeExpr* lowerAggregateSegment(FileLine* flp, const VType& type,
                                       const AggregateSegment& segment);
    AstNodeExpr* assembleAggregate(FileLine* flp, const VType& type,
                                   const std::vector<AggregateSegment>& segments);
    AstNodeExpr* lowerAggregate(const Expr& expr, const VType& type);
    AstNodeExpr* lowerAggregateValue(const Expr& expr, const VType& type);
    //! Replicate an element value `count` times, folding a constant and chunking a
    //! run-time replication to stay below the Verilator replication limit
    AstNodeExpr* repeatValue(FileLine* flp, AstNodeExpr* valuep, int count, AstNodeDType* dtp);
    bool deriveAggregateType(const Expr& expr, VType& out);
    AstConst* foldStatic(AstNodeExpr* exprp) const;
    AstNodeExpr* lowerSelectedExpr(const Expr& expr);
    const VType* typeOfName(const Expr& expr) const;
    const VType* discreteRangeType(const Expr& expr) const;
    bool typeOfDesignator(const Expr& expr, VType& out) const;
    bool packageNameOf(const Expr& prefix, std::string& name) const;
    AstNodeExpr* lowerAlias(const Expr& nameExpr, bool write);
    void registerAlias(const AliasDecl& alias);
    void registerSubprogramAlias(const AliasDecl& alias);
    AstNodeExpr* lowerPackageMember(const Expr& expr, const std::string& pkgName);
    VType typeOfTarget(const Expr& target, AstNodeExpr* lhsp) const;
    AstNodeExpr* lowerAlternatives(const Concurrent& conc, const VType& type);
    AstNodeStmt* lowerLatchedAlternatives(const Concurrent& conc, const VType& type,
                                          AstNodeExpr* lhsp);
    AstNodeStmt* lowerClockedAlternatives(const Concurrent& conc, const VType& type,
                                          AstNodeExpr* lhsp);
    bool concHasEdge(const Concurrent& conc) const;
    AstNodeStmt* lowerSelected(const Concurrent& conc);
    //! Lowered pieces of a selected assignment, shared by its conditional and case forms
    struct SelectedLowering final {
        FileLine* flp = nullptr;  // Source location
        const Concurrent* concp = nullptr;  // The selected assignment
        VType targetType;  // Resolved type of the target
        VType selectorType;  // Resolved type of the selector
        AstNodeDType* selDtp = nullptr;  // Data type of the selector
        AstNodeExpr* selectorp = nullptr;  // Lowered selector expression
        AstNodeExpr* lhsp = nullptr;  // Lowered target
        std::vector<AstNodeExpr*> values;  // Lowered value of each alternative
        bool tristate = false;  // A value is high-impedance, so the assignment is tristate

        void deleteValues(std::size_t from) const {
            for (std::size_t i = from; i < values.size(); ++i) {
                if (values[i]) values[i]->deleteTree();
            }
        }
    };
    bool lowerSelectedValues(SelectedLowering& ctx);
    AstNodeStmt* lowerSelectedTristate(SelectedLowering& ctx);
    AstNodeStmt* lowerSelectedCase(SelectedLowering& ctx);
    void emitConcurrent(const Concurrent& conc);
    void emitGenerateFor(const Concurrent& stmt);
    void emitLocalDecls(const std::vector<Decl>& decls, const std::vector<TypeDecl>& types,
                        const char* prefix);
    bool lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                           std::vector<AstConst*>& cops);
    //! Add the values one choice list covers; a value already there is a duplicate
    //! or overlapping choice (IEEE 1076-2008 10.9)
    static bool addChoiceValues(const std::vector<AstConst*>& cops, std::set<int64_t>& covered,
                                const char* what);
    //! Whether the choices cover every value the selector can take; a selector too
    //! wide to enumerate needs an `others` arm (IEEE 1076-2008 10.9)
    bool choicesComplete(AstNodeDType* selDtp, const VType& selType,
                         const std::set<int64_t>& covered) const;
    //! Whether a type has a resolution function: std_logic and the arrays built from
    //! it do, so several drivers are legal (IEEE 1076-2008 6.4.2.3)
    static bool isResolvedType(const VType& type) {
        if (type.typeName == "std_logic" || type.typeName == "std_logic_vector"
            || type.typeName == "unsigned" || type.typeName == "signed") {
            return true;
        }
        // A composite is resolved when every scalar subelement is (IEEE 1076-2008
        // 6.4.2.3); a record's fields and an array's element carry their own types
        if (type.isVector()) return type.elemp && isResolvedType(*type.elemp);
        if (type.kind == VType::RECORD) {
            if (type.fields.empty()) return false;
            for (const VField& field : type.fields) {
                if (!isResolvedType(*field.type)) return false;
            }
            return true;
        }
        return false;
    }
    //! Report every unresolved signal that more than one emitted assignment drives
    void checkDrivers();
    //! Emit `if (cond) $error(message); $finish;` as a module-level run-time check
    void addRuntimeError(FileLine* flp, AstNodeExpr* condp, const std::string& message);
    bool aggregateBounds(const Expr& agg, int64_t& low, int64_t& high) const;
    //! Bounds an unconstrained array target takes from an aggregate's own choices
    void constrainAggregateType(const Expr& agg, VType& type) const;
    AstNodeStmt* lowerStmt(const Stmt& stmt);
    //! One concurrent assignment, with the statements an inlined call's locals produced
    void emitConcurrentAssign(const Concurrent& stmt);
    void emitClockedConcurrentAssign(const Concurrent& stmt, const VType& ltype,
                                     AstNodeExpr* lhsp);
    void emitConcurrentWaveform(const Concurrent& stmt, const VType& ltype, AstNodeExpr* lhsp,
                                AstNodeExpr* rhsp);
    AstNodeStmt* lowerStmtList(const std::vector<StmtP>& stmts);
    AstNodeStmt* lowerStmtRange(const std::vector<StmtP>& stmts, std::size_t begin,
                                std::size_t end);
    AstNodeStmt* lowerIf(const Stmt& stmt);
    AstNodeStmt* lowerAssign(const Stmt& stmt, bool concurrent, bool variable = false);
    //! An assignment to a slice whose width is a run-time value: one guarded write per
    //! possible width, since a selection node needs a static width
    AstNodeStmt* lowerDynamicSliceAssign(const Stmt& stmt, bool variable);
    //! A sequential conditional assignment `v1 when c1 else ... vN` as a value expression;
    //! targetReadp supplies the value for an `unaffected` waveform (a read of the target)
    AstNodeExpr* lowerConditionalChain(FileLine* flp,
                                       const std::vector<std::pair<ExprP, ExprP>>& alternatives,
                                       const VType& type, AstNodeExpr* targetReadp);
    //! Split `(a, b) <= value` into one assignment per target, leftmost most significant
    AstNodeStmt* lowerAggregateAssign(FileLine* flp, const Expr& target, const Expr& value,
                                      bool variable, bool concurrentProc);
    void lowerProcess(const Process& process);
    bool resolveProcessSensitivity(const Process& process, const std::set<std::string>& varNames,
                                   std::vector<AstVar*>& sensVars);
    AstSenItem* buildProcessSensitivity(const Process& process,
                                        const std::vector<AstVar*>& sensVars,
                                        const std::vector<Edge>& edges,
                                        const std::set<std::string>& varNames);
    //! Emit the asynchronous-reset flip-flop idiom as an edge-triggered always, whose
    //! sensitivity then carries the clock. False when the process is not that idiom.
    bool lowerFlopIdiom(const Process& process);
    void registerSubprograms(const std::vector<SubprogramDecl>& decls, int frame,
                             bool processLocal = false);
    void registerComponents(const std::vector<ComponentDecl>& components);
    void setScope(const ScopeSave& saved);
    bool subprogramMatches(const Expr& call, const SubprogramDecl& sub, std::string& why) const;
    static std::vector<std::pair<const FormalDecl*, std::string>>
    formalSlots(const SubprogramDecl& sub);
    bool markKind(const std::string& mark, VType::Kind& out) const;
    bool actualKind(const Expr& expr, VType::Kind& out) const;
    bool actualType(const Expr& expr, VType& out) const;
    bool actualTypeUnary(const Expr& expr, VType& out) const;
    bool actualTypeBinary(const Expr& expr, VType& out) const;
    bool actualTypeName(const Expr& expr, VType& out) const;
    bool actualTypeIndex(const Expr& expr, VType& out) const;
    bool actualTypeAttr(const Expr& expr, VType& out) const;
    bool actualTypeCall(const Expr& expr, VType& out) const;
    bool formalType(const TypeSpec& spec, VType& out) const;
    static bool typesMatch(const VType& formal, const VType& actual);
    static bool kindAccepts(VType::Kind formal, VType::Kind actual);
    //! Whether a value belongs to the target's base type, for an assignment, a return
    //! or a generic actual (IEEE 1076-2008 10.5.1, 10.5.2, 12.5)
    bool valueConforms(const VType& target, const Expr& value, const char* what,
                       bool checkShape = true) const;
    static bool isNumericLiteral(const Expr& expr);
    static bool sameShape(const VType& target, const VType& value);
    //! Whether a type conversion's target and operand types are closely related
    //! (IEEE 1076-2008 9.3.6)
    static bool conversionRelated(const VType& target, const VType& source);
    //! Whether two operands of a logical operator have the same element type
    bool logicalOperandsConform(const Expr& a, const Expr& b, const VType& aLowered,
                                const VType& bLowered) const;
    //! Whether two operands of a relational operator have related base types
    bool relationalOperandsConform(const Expr& a, const Expr& b) const;
    bool formalsAccept(const Expr& call, const SubprogramDecl& sub) const;
    bool formalsExact(const Expr& call, const SubprogramDecl& sub) const;
    std::vector<const SubprogramDecl*> visibleSubprograms(const std::string& name) const;
    const SubprogramDecl* resolveCall(const Expr& call, bool function,
                                      const SubprogramRef** foundpp = nullptr,
                                      const VType* contextp = nullptr);
    //! A user-defined infix operator whose formals exactly match the operands
    AstNodeExpr* lowerUserOperator(const Expr& expr, const VType* contextp, bool& found);
    //! A user-defined prefix operator whose formal matches the operand
    AstNodeExpr* lowerUserUnary(const Expr& expr, const VType* contextp, bool& found);
    void captureBaseFor(const SubprogramRef& ref, ScopeSave& base, InlineEnv& env) const;
    AstNodeExpr* inlineBinding(FileLine* flp, const std::string& name, bool write) const;
    bool bindSubprogramFormals(const Expr& call, const SubprogramDecl& sub, InlineEnv& env,
                               AstNodeStmt*& prologsp, bool lvalueFormals, bool concurrent);
    AstNodeExpr* lowerFunctionCall(const Expr& call, const VType* contextp,
                                   const SubprogramDecl* forcedp = nullptr);
    const SubprogramRef* visibleRef(const SubprogramDecl* declp) const;
    bool bindFunctionLocals(const SubprogramDecl& subp, InlineEnv& env);
    void noteReturnedType(InlineFrame& frame, const Expr& value, AstNodeExpr* valuep);
    AstNodeExpr* adoptCallResultType(AstNodeExpr* resultp, const SubprogramDecl& subp,
                                     const VType& retType, const VType* contextp, FileLine* flp);
    AstNodeExpr* evalFuncBlock(const std::vector<StmtP>& stmts, std::size_t index, InlineEnv& env,
                               const InlineCont& cont, const InlineCont* exitContp, int depth);
    bool evalFuncVarAssign(const Stmt& stmt);
    bool evalFuncElementAssign(const Stmt& stmt);
    AstNodeExpr* evalFuncExitNext(const std::vector<StmtP>& stmts, std::size_t i,
                                  const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncBlockBody(const std::vector<StmtP>& stmts, std::size_t index,
                                   const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncBranches(const Stmt& stmt, std::size_t branchIndex,
                                  const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncCase(const Stmt& stmt, const InlineCont& cont,
                              const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncFor(const Stmt& stmt, const InlineCont& postCont, int depth);
    bool conjunctFolds(const Expr& cond, bool value);
    AstNodeExpr* evalFuncWhile(const Stmt& stmt, const InlineCont& postCont, int depth);
    AstNodeExpr* updateLocalElement(FileLine* flp, AstNodeExpr* oldp, AstNodeExpr* valuep,
                                    AstNodeDType* elemDtp, AstNodeExpr* lsbp, int width);
    AstNodeStmt* lowerProcedureCall(const Expr& callExpr, bool concurrent);
    AstNodeStmt* lowerProcChain(const std::vector<const Stmt*>& stmts, std::size_t index,
                                bool& fallsThroughp);
    AstNodeStmt* lowerProcChainWithTail(const std::vector<StmtP>& stmts,
                                        const std::vector<const Stmt*>& tail, bool& fallsThroughp);
    AstNodeStmt* lowerProcIf(const Stmt& stmt, std::size_t branchIndex,
                             const std::vector<const Stmt*>& tail, bool& fallsThroughp);
    AstNodeStmt* lowerProcCase(const Stmt& stmt, const std::vector<const Stmt*>& tail,
                               bool& fallsThroughp);
    static void deleteInlineEnv(InlineEnv& env);
    static InlineEnv cloneInlineEnv(const InlineEnv& env);
    static std::vector<const Stmt*> rawPtrs(const std::vector<StmtP>& stmts);

public:
    explicit Lowering(AstNetlist* rootp, const PackageTable::Impl* packagesp = nullptr,
                      const ModuleMap* modulesp = nullptr, Design* designp = nullptr)
        : m_rootp{rootp}
        , m_modulesp{modulesp}
        , m_packagesp{packagesp}
        , m_designp{designp} {}
    ~Lowering() = default;

    bool emit(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
              const GenericBindings* bindingsp = nullptr,
              const PortBoundsMap* portBoundsp = nullptr);
    AstNode* lowerInstance(const Concurrent& conc);
    //! One generic of an entity, as seen from an instance's generic map
    struct InstanceGeneric final {
        const GenericDecl* declp;  // The generic declaration
        std::string folded;  // Folded name
        std::string spelling;  // As written
    };

    //! State shared while binding one instance's port associations
    struct InstancePortBind final {
        const Concurrent& conc;  // Instance statement binding the ports
        const EntityUnit* entityp;  // Entity being instantiated, null when unknown
        const PortBoundsMap& portBounds;  // Bounds of the unconstrained ports, from the instance
        bool defaultError;  // The port default was rejected, with its error reported
    };

    bool emitPrelude(const EntityUnit& entity, const Architecture& arch,
                     const GenericBindings* bindingsp);
    void emitEntityDecls(const EntityUnit& entity, const Architecture& arch);
    void emitPorts(const EntityUnit& entity, const PortBoundsMap* portBoundsp);
    void emitArchitectureDecls(const Architecture& arch);

    const Architecture* selectInstanceArchitecture(const Concurrent& conc,
                                                   const EntityUnit& entity);
    static void freeGenericBindings(GenericBindings& bindings);
    bool bindTypeGeneric(const PortAssoc& assoc, const std::string& folded,
                         GenericBindings& bindings);
    bool bindSubprogramGeneric(const PortAssoc& assoc, const std::string& folded,
                               GenericBindings& bindings);
    bool bindRealGeneric(const PortAssoc& assoc, const std::string& folded,
                         GenericBindings& bindings);
    bool bindStringGeneric(const InstanceGeneric& item, const PortAssoc& assoc,
                           GenericBindings& bindings);
    bool bindValueGeneric(const Concurrent& conc, const EntityUnit& entity,
                          const InstanceGeneric& item, const PortAssoc& assoc,
                          GenericBindings& bindings);
    bool bindOneGeneric(const Concurrent& conc, const EntityUnit& entity,
                        const InstanceGeneric& item, const PortAssoc* assocp,
                        GenericBindings& bindings);
    bool bindInstanceGenerics(const Concurrent& conc, const EntityUnit& entity,
                              GenericBindings& bindings);

    bool lookupInstanceModule(const Concurrent& conc, AstModule*& modp,
                              const EntityUnit*& entityp);
    bool checkComponentConformance(const Concurrent& conc, const EntityUnit& entity);
    bool selectInstanceArchitecture(const Concurrent& conc, const EntityUnit* entityp,
                                    AstModule*& modp);
    bool specializeInstancePorts(const Concurrent& conc, const EntityUnit* entityp,
                                 AstModule*& modp, PortBoundsMap& portBounds);
    bool collectInstancePorts(const Concurrent& conc, AstModule* modp,
                              std::vector<AstVar*>& ports);
    VType instanceRecordPortType(const EntityUnit* entityp, const AstVar* portp) const;
    static const VField* findRecordField(const VType& rec, const std::string& member);
    AstNodeExpr* instanceDeclaredDefault(const Concurrent& conc, const EntityUnit* entityp,
                                         const AstVar* portp, const VType& aggType,
                                         const VType& portType, bool& defaultError);
    bool bindRecordMemberPort(InstancePortBind& ctx, AstVar* portp,
                              const std::vector<const PortAssoc*>& assocs, const VType& recType,
                              AstNodeExpr*& actualp);
    bool bindWholePort(InstancePortBind& ctx, AstVar* portp, const PortAssoc& assoc,
                       const VType& aggType, const VType& portType, AstNodeExpr*& actualp);
    bool bindPartialPort(InstancePortBind& ctx, AstVar* portp,
                         const std::vector<const PortAssoc*>& assocs, const VType& portType,
                         AstNodeExpr*& actualp);
    bool bindInstancePort(InstancePortBind& ctx, AstVar* portp, size_t portIndex, AstPin*& pinsp);
    bool hasDeferredConsts() const { return !m_deferredConsts.empty(); }
    std::string deferredOwner() const {
        return m_deferredConsts.empty() ? std::string{} : m_deferredConsts.front().owner;
    }

    void emitPackage(const PackageUnit& pkg, PackageTable::Impl& table,
                     const GenericBindings* bindingsp = nullptr,
                     const std::string& publishName = {});
    void completeDeferredConsts();
    PackageInfo publishedInfo(const PackageUnit& pkg);
    void instantiatePackage(const std::vector<PackageUnit>& packages, const PackageInstance& inst,
                            PackageTable::Impl& table);
};

// Helpers shared by the lowering translation units
bool predefinedUnit(const std::string& unit, int64_t& out);
bool physicalValue(const Design& design, const Expr& expr, int64_t& out);
void addPhysicalUnits(const TypeDecl& decl, Design& design);
bool mentionsRealValue(const Expr& expr);
bool isIndexRangeAttribute(const std::string& attribute);
bool aggregateHasOthers(const Expr& expr);
VType cloneArrayChain(const VType& type);
std::string stringTextOfConst(AstConst* constp);
bool astHasZ(AstNode* nodep);
std::string reportText(const Expr* exprp, const char* fallback);
bool homograph(const SubprogramDecl& lhs, const SubprogramDecl& rhs);
bool readsFile(const SubprogramDecl& subp);
bool isUnconstrainedMark(const std::string& mark);
bool isVectorKind(const VType::Kind kind);
bool integralRealLiteral(const Expr& expr, double& value);
bool fileReadValue(const std::string& line, size_t& pos, int width, bool hex, std::string& bits);
bool unconstrainedVectorSpec(const TypeSpec& spec);
bool isBuiltinGenericMark(const std::string& mark);
bool memberTypeUnconstrained(const TypeSpec& type);

}  // namespace vhdlnative
#endif  // Guard
