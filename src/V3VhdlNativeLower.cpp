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
// match what V3VhdlLink expects. VHDL identifiers are case-insensitive, so a name
// is emitted under its folded (lowercase) form -- the spelling as written stays
// available for diagnostics only.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNativeLower.h"

#include "V3Error.h"
#include "V3FileLine.h"

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
    enum Kind : uint8_t {
        LOGIC, BIT, BOOLEAN, INT, VECTOR, ARRAY, RECORD, ENUM, REAL, FILE, LINE
    } kind = LOGIC;
    AstNodeDType* dtp = nullptr;
    int width = 1;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    std::string typeName;  // Base type identity for overload resolution
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
    //! A VHDL array type, as opposed to a numeric_std unsigned/signed vector
    bool isVhdlArray() const {
        return signing == VSigning::NOSIGN && (kind == VECTOR || kind == ARRAY);
    }
    //! A predefined 1-D array type such as std_logic_vector or bit_vector (or a
    //! user-declared array of a one-bit element, which compares the same way)
    bool isPredefinedVector() const {
        return (kind == VECTOR && signing == VSigning::NOSIGN)
               || (kind == ARRAY && signing == VSigning::NOSIGN && elemWidth == 1);
    }
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
    std::set<std::string> ambiguousLiterals;  // Literals declared by more than one type
    std::map<std::string, double> realConsts;  // Elaboration-time reals, by folded name
    std::vector<const SubprogramDecl*> subprograms;  // Prototypes and bodies, by declaration
    std::vector<const AliasDecl*> subprogramAliases;  // Synonyms, re-registered on import
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
    bool isString = false;
    std::string stringValue;
    bool isType = false;
    VType typeValue;  // The actual type mark of a generic type parameter
    AstConst* valuep = nullptr;  // Typed constant; cloned when registered, else owned here
    const SubprogramDecl* subp = nullptr;  // The actual of a generic subprogram parameter
};
using GenericBindings = std::map<std::string, GenericBinding>;

//! The multiple of the step unit that a predefined unit of `time' stands for, or false for
//! any other name. A physical type's own unit of the same name shadows the predefined one.
static bool predefinedUnit(const std::string& unit, int64_t& out) {
    // The step unit of `time' is the femtosecond, so 2 sec is 2e15 and needs 64 bits
    static const std::pair<const char*, int64_t> timeUnits[] = {{"fs", 1},
                                                                {"fsec", 1},
                                                                {"ps", 1000},
                                                                {"psec", 1000},
                                                                {"ns", 1000000},
                                                                {"nsec", 1000000},
                                                                {"us", 1000000000},
                                                                {"usec", 1000000000},
                                                                {"ms", 1000000000000},
                                                                {"msec", 1000000000000},
                                                                {"sec", 1000000000000000},
                                                                {"min", 60000000000000000},
                                                                {"hr", 3600000000000000000}};
    for (const std::pair<const char*, int64_t>& timeUnit : timeUnits) {
        if (unit == timeUnit.first) {
            out = timeUnit.second;
            return true;
        }
    }
    return false;
}

//! Value of a physical literal: the abstract literal scaled to its type's step unit. The
//! literal names its unit but not its type, so the name is looked up in the table every
//! physical type's declaration adds to, which spans the design.
static bool physicalValue(const Design& design, const Expr& expr, int64_t& out) {
    if (design.ambiguousUnits.count(expr.text)) {
        unsupportedError(expr.flp, "physical unit '" + expr.spelling + "' of two types");
        return false;
    }
    int64_t factor = 1;  // An abstract literal written without a unit names the step unit
    if (!expr.text.empty()) {
        const auto it = design.unitFactors.find(expr.text);
        if (it != design.unitFactors.end()) {
            factor = it->second;
        } else if (!predefinedUnit(expr.text, factor)) {
            expr.flp->v3error("Unknown VHDL physical unit: '" + expr.spelling + "'");
            return false;
        }
    }
    out = std::llround(expr.realValue * static_cast<double>(factor));
    return true;
}

//! Record one physical type's unit names and their multiples of the step unit. The first
//! declaration sets the step; each later one names an earlier unit of any physical type, or
//! a predefined one.
static void addPhysicalUnits(const TypeDecl& decl, Design& design) {
    int64_t factor = 1;
    for (const TypeDecl::Unit& unit : decl.units) {
        if (unit.value && !physicalValue(design, *unit.value, factor)) return;
        const auto seen = design.unitFactors.find(unit.name);
        // Two physical types that are not each other's scope may give one name a different
        // multiple; only the expected type tells a literal which one it means
        if (seen != design.unitFactors.end() && seen->second != factor) {
            design.ambiguousUnits.insert(unit.name);
        }
        design.unitFactors[unit.name] = factor;
    }
}

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
        std::map<std::string, int64_t> preConsts;
        std::map<std::string, VType> types;
        std::set<std::string> ambiguousTypes;
        std::set<std::string> ambiguousLiterals;
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
    //! Cost of the element writes folded into one inlined body; each rebuilds the local value
    int64_t m_funcWriteCost = 0;
    std::map<std::string, std::vector<SubprogramRef>> m_subprograms;  // Visible, by name
    std::vector<const Stmt*> m_procTail;  // Statements after the current procedure return
    int m_concurrentProc = 0;  // Lowering a concurrent procedure call body
    ScopeSave m_moduleScope;  // Module declarative part, hidden by processes
    int m_procVarSeq = 0;  // Disambiguates procedure local variables
    const VType* m_funcRetp = nullptr;  // Return-type context for function bodies
    std::map<std::string, VarInfo> m_scope;  // Signals and ports by folded name
    // Symbolic values of process variables, for slice widths from their relations
    std::map<std::string, ExprP> m_varVals;
    std::map<std::string, int64_t> m_consts;  // Integer constants by folded name
    //! Integer constants needed while registering types, before full declaration emission
    std::map<std::string, int64_t> m_preConsts;
    //! Elaboration-time real constants (IEEE 1800 `real`) by folded name
    std::map<std::string, double> m_realConsts;
    //! A package specification constant whose initializer calls a subprogram with no body
    //! yet: the body's pass completes it
    struct DeferredConst final {
        std::vector<std::pair<std::string, std::string>> names;
        const TypeSpec* typep = nullptr;
        const Expr* initp = nullptr;
        FileLine* flp = nullptr;
        std::string owner;  // Unit that declared it: only that unit completes it
    };
    std::vector<DeferredConst> m_deferredConsts;
    std::string m_unitName;  // Unit being emitted, which owns its deferred constants
    //! A pure function call the elaboration-time folders are evaluating: its formals
    //! bound to the values of the actuals, plus the declaration to stop a body folding
    //! into itself. Only the innermost frame is in scope and every actual is already
    //! valued, so an evaluation over a body cannot re-enter itself. Pushed and popped
    //! around one evaluation, so this is empty outside a fold.
    struct PureCall final {
        std::map<std::string, ExprP> args;
        const SubprogramDecl* subp = nullptr;
    };
    mutable std::vector<PureCall> m_pureCalls;
    std::map<std::string, VType> m_types;  // Declared types by folded name
    //! A type the declarative part deferred, with the sequence number of the part that
    //! deferred it: `resolveType` registers one at its first use, once the emitter has
    //! computed the constant its constraint names, and only its own part may finish it
    struct DeferredType final {
        const TypeDecl* declp;
        size_t seq;
    };
    std::map<std::string, DeferredType> m_deferredTypes;
    size_t m_deferSeq = 0;  // Sequence number of the part being registered
    std::map<std::string, AstConst*> m_constValues;  // Typed constants by folded name
    std::map<std::string, VType> m_constTypes;  // Type of each typed constant
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
        std::ifstream in;
        bool eof = false;
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
    std::vector<std::pair<std::string, std::string>> m_pendingNames;  // Duplicate detection
    struct Edge final {
        AstVar* varp = nullptr;
        VEdgeType type = VEdgeType::ET_CHANGED;
    };
    std::vector<Edge> m_edges;  // Clock edges collected while lowering a process
    bool m_initial = false;  // Emitting the time-zero evaluation
    //! The process has one sensitivity signal, so it runs on every event of that signal
    //! and an edge call is that signal's level rather than a constant
    bool m_edgeAsLevel = false;
    //! The process has several sensitivity signals, so an edge call must compare the
    //! signal with its value at the previous run of the process
    bool m_edgeAsPrev = false;
    std::map<AstVar*, AstVar*> m_edgePrevVars;  // Edge signal -> its previous value
    int m_edgeSeq = 0;  // Disambiguates the previous-value variables
    //! Set while a wait process lowers its body: a bare `wait;` reached at run time
    //! sets this flag, which gates every later activation
    AstVar* m_suspendFlagp = nullptr;
    int m_varSeq = 0;  // Disambiguates a process's own variables module-wide
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
    std::set<std::string> m_ambiguousTypes;  // Type names imported from two packages
    std::set<std::string> m_ambiguousLiterals;  // Enumeration literals from two types
    bool m_stdLogicSigned = false;  // `use ieee.std_logic_signed.all` is in effect
    bool m_stdLogicArith = false;  // `use ieee.std_logic_arith.all` is in effect
    //! Element structure of an array dtype, including a function result's return type
    std::map<AstNodeDType*, VType> m_arrayDtypes;
    std::set<std::string> m_variables;  // Module-level variables, which are not signals
    //! Signals whose type has no resolution function: at most one driver (IEEE 1076-2008
    //! 12.6.4), unlike std_logic and the arrays built from it
    std::set<AstVar*> m_unresolvedSignals;
    std::map<std::string, AliasInfo> m_aliases;
    mutable std::set<std::string> m_aliasBusy;  // Aliases being expanded (recursion guard)
    Design* const m_designp;  // Design context for generic specialization

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
    //! which a packed dtype no longer carries.
    VType typeOfLowered(const Expr& expr, AstNodeExpr* nodep) const {
        VType type;
        if (typeOfDesignator(expr, type) && type.width == nodep->width()) return type;
        const auto arrayIt = m_arrayDtypes.find(nodep->dtypep());
        if (arrayIt != m_arrayDtypes.end() && arrayIt->second.width == nodep->width()) {
            return arrayIt->second;
        }
        return typeOfDType(nodep->dtypep());
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
    //! Register a declarative part's types, deferring one whose constraint names a constant
    //! of the same part written above it that the pre-scan did not fold: the part's
    //! constants are emitted after all of its types, so only a first use can see the value
    void registerTypes(const std::vector<TypeDecl>& types, const std::vector<Decl>& decls);
    //! Register the deferred types, reporting one whose constraint still cannot resolve
    void finishDeferredTypes();
    static bool exprNamesAny(const Expr& expr, const std::set<std::string>& names);
    static bool typeDeclNamesAny(const TypeDecl& decl, const std::set<std::string>& names);
    static bool typeDeclMarksAny(const TypeDecl& decl, const std::set<std::string>& names);
    //! Make a package visible; `only` names one declaration to import instead
    void importPackage(const std::string& name, const std::string& only = "");
    //! Record integer constants that a type declaration may depend on
    void preScanIntConstants(const std::vector<Decl>& decls);
    void emitConstant(const std::vector<std::pair<std::string, std::string>>& names,
                      const TypeSpec& type, const Expr* initp, FileLine* flp, bool deferredOk);
    bool registerGenerics(const std::vector<GenericDecl>& generics,
                          const GenericBindings* bindings, bool probe);
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
    //! Constrain one record element to a fixed vector range
    void setRecordField(FileLine* flp, VField& field, int left, int right, bool ascending);
    //! Recompute a record's total width, MSB-first offsets and packed dtype
    void finalizeRecordFields(FileLine* flp, VType& rec);
    bool constraintBounds(const TypeSpec& spec, int64_t& left, int64_t& right, bool& ascending);
    //! The index range an array attribute names: `A'HIGH(2)` is the second one, and an
    //! attribute written without an index is the first, IEEE 1076-2008 16.2.3
    const VType* indexRangeLevel(const Expr& expr, const VType& type) const;
    bool evalInt(const Expr& expr, int64_t& value) const;
    //! Evaluate a locally static real-valued expression
    bool evalReal(const Expr& expr, double& value) const;
    //! Evaluate a locally static condition: a literal, a comparison of literals, or a
    //! pure function of literals returning one. An elaboration-time check such as
    //! `assert is_power_of_two(n)` folds, so its callee's body is never lowered.
    bool evalCond(const Expr& expr, bool& value) const;
    bool mentionsReal(const Expr& expr) const;
    bool rejectRealObject(const VType& type, FileLine* flp, bool allowUnconstrained = false);
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
    static bool combineLinear(const Expr* atomA, int64_t coefA, int64_t conA, const Expr* atomB,
                              int64_t coefB, int64_t conB, int64_t sign, const Expr*& atomOut,
                              int64_t& coefOut, int64_t& conOut) {
        const int64_t bCoef = coefB * sign;
        const int64_t bCon = conB * sign;
        if (coefA == 0) {
            atomOut = atomB;
            coefOut = bCoef;
            conOut = conA + bCon;
            return true;
        }
        if (bCoef == 0) {
            atomOut = atomA;
            coefOut = coefA;
            conOut = conA + bCon;
            return true;
        }
        if (!sameExpr(*atomA, *atomB)) return false;  // two distinct dynamic terms
        atomOut = atomA;
        coefOut = coefA + bCoef;
        conOut = conA + bCon;
        return true;
    }
    //! Parse an expression into coef * atom + const: fold constants, add/subtract like
    //! terms and scale by a constant multiplicand; atomOut is null for a pure constant
    bool linearForm(const Expr& expr, const Expr*& atomOut, int64_t& coefOut,
                    int64_t& conOut) const {
        const Expr* e = &expr;
        while (e->kind == Expr::PAREN && e->a) e = e->a.get();
        atomOut = nullptr;
        coefOut = 0;
        conOut = 0;
        int64_t value = 0;
        if (evalInt(*e, value)) {
            conOut = value;
            return true;
        }
        if (e->kind == Expr::BINARY && e->a && e->b) {
            const Expr* aAtom = nullptr;
            const Expr* bAtom = nullptr;
            int64_t aCoef = 0;
            int64_t aCon = 0;
            int64_t bCoef = 0;
            int64_t bCon = 0;
            const int sign = e->text == "+" ? 1 : e->text == "-" ? -1 : 0;
            if (sign != 0 && linearForm(*e->a, aAtom, aCoef, aCon)
                && linearForm(*e->b, bAtom, bCoef, bCon)) {
                return combineLinear(aAtom, aCoef, aCon, bAtom, bCoef, bCon, sign, atomOut,
                                     coefOut, conOut);
            }
            if (e->text == "*") {
                if (evalInt(*e->b, value) && linearForm(*e->a, aAtom, aCoef, aCon)) {
                    atomOut = aAtom;
                    coefOut = aCoef * value;
                    conOut = aCon * value;
                    return true;
                }
                if (evalInt(*e->a, value) && linearForm(*e->b, bAtom, bCoef, bCon)) {
                    atomOut = bAtom;
                    coefOut = bCoef * value;
                    conOut = bCon * value;
                    return true;
                }
            }
        }
        // An opaque dynamic term: 1 * e + 0
        atomOut = e;
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
        if (expr.a) copy->a = substVars(*expr.a, depth + 1);
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
        const Expr* hiAtom = nullptr;
        const Expr* loAtom = nullptr;
        int64_t hiCoef = 0;
        int64_t hiCon = 0;
        int64_t loCoef = 0;
        int64_t loCon = 0;
        if (!linearForm(hip, hiAtom, hiCoef, hiCon) || !linearForm(lop, loAtom, loCoef, loCon)) {
            return false;
        }
        if (hiCoef != loCoef || (hiCoef != 0 && !sameExpr(*hiAtom, *loAtom))) return false;
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

    //! An unconstrained vector object whose value is a bit string: the mark leaves the
    //! width open and the literal fixes it, e.g. `constant c : std_logic_vector := "0011"`
    bool isLiteralSizedVector(const TypeSpec& type, const Expr* initp) const {
        if (!initp || initp->kind != Expr::BITS_LIT) return false;
        if (type.hasRange || !type.constraintMark.empty()) return false;
        return type.mark == "std_logic_vector" || type.mark == "std_ulogic_vector"
               || type.mark == "bit_vector" || type.mark == "unsigned" || type.mark == "signed";
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
        type.typeName = mark;
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
                                            "and_reduce",
                                            "or_reduce",
                                            "xor_reduce",
                                            "nand_reduce",
                                            "nor_reduce",
                                            "xnor_reduce",
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
               || name == "positive";
    }

    AstNodeExpr* lowerExpr(const Expr& expr, const VType* context);
    AstNodeExpr* lowerBinary(const Expr& expr, const VType* context);
    //! Lexicographic ordering of two VHDL arrays; consumes the operands
    AstNodeExpr* lowerArrayRelation(FileLine* flp, const std::string& cmpOp, const VType& lt,
                                    const VType& rt, AstNodeExpr*& lp, AstNodeExpr*& rp,
                                    AstNodeDType* bitDtp);
    AstNodeExpr* lowerConversion(const Expr& expr);
    AstNodeExpr* lowerDesignator(const Expr& expr, bool write);
    AstNodeExpr* lowerSel(const Expr& expr, AstNodeExpr* basep, const VType& baseType, bool write);
    bool sliceBound(const ExprP& expr, const std::string& prefix, const VType& baseType,
                    int64_t& out);
    AstNodeExpr* sliceOf(FileLine* flp, AstNodeExpr* basep, const VType& baseType, int elemWidth,
                         int hi, int lo, bool ascending);

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
    AstNode* lowerForRuntime(const Stmt& stmt);
    AstNode* lowerReport(const Stmt& stmt);
    //! A failing assertion reports, and `severity failure` ends the run
    AstNode* lowerAssert(const Stmt& stmt);
    AstNode* assertBody(FileLine* flp, const Expr& condition, const Expr* message,
                        Stmt::Severity severity);
    AstNode* lowerDelay(const Expr& expr, FileLine* flp);
    static void collectReadNames(const std::vector<StmtP>& stmts, std::set<std::string>& names);
    bool stmtsContainEdge(const std::vector<StmtP>& stmts) const;
    static void collectExprNames(const Expr& expr, std::set<std::string>& names);
    //! The names an assignment target reads: an indexed name evaluates its index
    //! and a slice its bounds (IEEE 1076-2008 8.4); the target name is only written
    static void collectTargetReadNames(const Expr& expr, std::set<std::string>& names);
    static bool usesExitNext(const std::vector<StmtP>& stmts);
    static bool usesBareWait(const std::vector<StmtP>& stmts);
    static bool usesSeverityFailure(const std::vector<StmtP>& stmts);
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
    AstNode* lowerLatchedAlternatives(const Concurrent& conc, const VType& type,
                                      AstNodeExpr* lhsp);
    AstNode* lowerClockedAlternatives(const Concurrent& conc, const VType& type,
                                      AstNodeExpr* lhsp);
    bool concHasEdge(const Concurrent& conc) const;
    AstNode* lowerSelected(const Concurrent& conc);
    void emitConcurrent(const Concurrent& conc);
    void emitLocalDecls(const std::vector<Decl>& decls, const std::vector<TypeDecl>& types,
                        const char* prefix);
    bool lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                           std::vector<AstConst*>& cops);
    //! Add the values one choice list covers; a value already there is a duplicate
    //! or overlapping choice (IEEE 1076-2008 10.9, 11.5)
    static bool addChoiceValues(const std::vector<AstConst*>& cops, std::set<int64_t>& covered,
                                const char* what);
    //! Whether the choices cover every value the selector can take; a selector too
    //! wide to enumerate needs an `others` arm (IEEE 1076-2008 10.9, 11.5)
    bool choicesComplete(AstNodeDType* selDtp, const std::set<int64_t>& covered) const;
    //! Whether a type has a resolution function: std_logic and the arrays built from
    //! it do, so several drivers are legal (IEEE 1076-2008 12.6.4)
    static bool isResolvedType(const VType& type) {
        if (type.typeName == "std_logic" || type.typeName == "std_logic_vector"
            || type.typeName == "unsigned" || type.typeName == "signed") {
            return true;
        }
        return type.kind == VType::ARRAY && type.elemp && type.elemp->typeName == "std_logic";
    }
    //! Report every unresolved signal that more than one emitted assignment drives
    void checkDrivers();
    bool aggregateBounds(const Expr& agg, int64_t& low, int64_t& high) const;
    AstNode* lowerStmt(const Stmt& stmt);
    AstNode* lowerStmtList(const std::vector<StmtP>& stmts);
    AstNode* lowerStmtRange(const std::vector<StmtP>& stmts, std::size_t begin, std::size_t end);
    AstNode* lowerIf(const Stmt& stmt);
    AstNode* lowerAssign(const Stmt& stmt, bool concurrent, bool variable = false);
    //! A sequential conditional assignment `v1 when c1 else ... vN` as a value expression;
    //! targetReadp supplies the value for an `unaffected` waveform (a read of the target)
    AstNodeExpr* lowerConditionalChain(FileLine* flp,
                                       const std::vector<std::pair<ExprP, ExprP>>& alternatives,
                                       const VType& type, AstNodeExpr* targetReadp);
    //! Split `(a, b) <= value` into one assignment per target, leftmost most significant
    AstNode* lowerAggregateAssign(FileLine* flp, const Expr& target, const Expr& value,
                                  bool variable, bool concurrentProc);
    void lowerProcess(const Process& process);
    //! Emit the asynchronous-reset flip-flop idiom as an edge-triggered always, whose
    //! sensitivity then carries the clock. False when the process is not that idiom.
    bool lowerFlopIdiom(const Process& process);
    void registerSubprograms(const std::vector<SubprogramDecl>& decls, int frame,
                             bool processLocal = false);
    void setScope(const ScopeSave& saved);
    bool subprogramMatches(const Expr& call, const SubprogramDecl& sub, std::string& why) const;
    static std::vector<std::pair<const FormalDecl*, std::string>>
    formalSlots(const SubprogramDecl& sub);
    bool markKind(const std::string& mark, VType::Kind& out) const;
    bool actualKind(const Expr& expr, VType::Kind& out) const;
    bool actualType(const Expr& expr, VType& out) const;
    bool formalType(const TypeSpec& spec, VType& out) const;
    static bool typesMatch(const VType& formal, const VType& actual);
    static bool kindAccepts(VType::Kind formal, VType::Kind actual);
    bool formalsAccept(const Expr& call, const SubprogramDecl& sub) const;
    bool formalsExact(const Expr& call, const SubprogramDecl& sub) const;
    std::vector<const SubprogramDecl*> visibleSubprograms(const std::string& name) const;
    const SubprogramDecl* resolveCall(const Expr& call, bool function,
                                      const SubprogramRef** foundpp = nullptr);
    //! A user-defined infix operator whose formals exactly match the operands
    AstNodeExpr* lowerUserOperator(const Expr& expr, const VType* context, bool& found);
    //! A user-defined prefix operator whose formal matches the operand
    AstNodeExpr* lowerUserUnary(const Expr& expr, const VType* context, bool& found);
    void captureBaseFor(const SubprogramRef& ref, ScopeSave& base, InlineEnv& env) const;
    AstNodeExpr* inlineBinding(FileLine* flp, const std::string& name, bool write) const;
    bool bindSubprogramFormals(const Expr& call, const SubprogramDecl& sub, InlineEnv& env,
                               AstNode*& prologsp, bool lvalueFormals, bool concurrent);
    AstNodeExpr* lowerFunctionCall(const Expr& call, const VType* context,
                                   const SubprogramDecl* forcedp = nullptr);
    AstNodeExpr* evalFuncBlock(const std::vector<StmtP>& stmts, std::size_t index, InlineEnv& env,
                               const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncBlockBody(const std::vector<StmtP>& stmts, std::size_t index,
                                   const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncBranches(const Stmt& stmt, std::size_t branchIndex,
                                  const InlineCont& cont, const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncCase(const Stmt& stmt, const InlineCont& cont,
                              const InlineCont* exitContp, int depth);
    AstNodeExpr* evalFuncFor(const Stmt& stmt, const InlineCont& postCont, int depth);
    AstNodeExpr* evalFuncWhile(const Stmt& stmt, const InlineCont& postCont, int depth);
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
              const GenericBindings* bindings = nullptr,
              const PortBoundsMap* portBounds = nullptr);
    AstNode* lowerInstance(const Concurrent& conc);
    bool hasDeferredConsts() const { return !m_deferredConsts.empty(); }
    std::string deferredOwner() const {
        return m_deferredConsts.empty() ? std::string{} : m_deferredConsts.front().owner;
    }

    void emitPackage(const PackageUnit& pkg, PackageTable::Impl& table,
                     const GenericBindings* bindings = nullptr,
                     const std::string& publishName = {});
    void instantiatePackage(const std::vector<PackageUnit>& packages, const PackageInstance& inst,
                            PackageTable::Impl& table);
};

//######################################################################
// Lowering

//! True when every value reference in `expr` is one of `args`, the formals of the
//! function being folded. The folders resolve a name in the caller's scope, so a body
//! mentioning one of its own package's names could fold to the wrong value; a closed
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
static bool isIndexRangeAttribute(const std::string& attribute) {
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
        // A universal_real in an integer context, IEEE 1800-2023 7.3.5
        if (std::isfinite(expr.realValue) && expr.realValue == std::floor(expr.realValue)) {
            value = static_cast<int64_t>(expr.realValue);
            return true;
        }
        return false;
    case Expr::NAME: {
        if (!m_pureCalls.empty()) {
            const auto argIt = m_pureCalls.back().args.find(expr.text);
            if (argIt != m_pureCalls.back().args.end()) return evalInt(*argIt->second, value);
        }
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
    case Expr::ATTR: {
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
            value = position;
            return true;
        }
        if (typep->kind != VType::ENUM) return false;
        const int64_t count = static_cast<int64_t>(typep->literals.size());
        if (attribute == "succ" || attribute == "pred") {
            const int64_t next = attribute == "succ" ? position + 1 : position - 1;
            if (position < 0 || position >= count) {
                expr.flp->v3error("VHDL: '" + std::string{attribute}
                                  + "' value is outside the enumeration type");
                return false;
            }
            if (next < 0 || next >= count) {
                expr.flp->v3error("VHDL: '" + std::string{attribute} + "' of the "
                                  + (attribute == "succ" ? "last" : "first")
                                  + " enumeration literal is outside the type");
                return false;
            }
            value = next;
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
    case Expr::INDEX: {
        if (!expr.a || expr.a->kind != Expr::NAME) return false;
        const Expr* returnp = nullptr;
        if (pushPureCall(expr, returnp)) {
            const bool folded = evalInt(*returnp, value);
            popPureCall();
            if (folded) return true;
        }
        if (expr.args.size() != 1) return false;
        const std::string& fn = expr.a->text;
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
    default: return false;
    }
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
    const SubprogramDecl* candidate = nullptr;
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
        if (candidate) return false;
        candidate = subp;
    }
    if (!candidate) return false;
    PureCall frame;
    std::size_t slot = 0;
    for (const FormalDecl& formal : candidate->formals) {
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
    frame.subp = candidate;
    m_pureCalls.push_back(std::move(frame));
    // Constants of the body's declarative part are folded on top of the formals, which a
    // helper such as `constant log2_value : natural := floor_log2(value);` needs
    for (const Decl& decl : candidate->decls) {
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
    if (!closedOverFormals(*candidate->body[0]->value, m_pureCalls.back().args)) {
        popPureCall();
        return false;
    }
    returnp = candidate->body[0]->value.get();
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
            const auto argIt = m_pureCalls.back().args.find(expr.text);
            if (argIt != m_pureCalls.back().args.end()) return evalReal(*argIt->second, value);
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
        if (!expr.a || expr.a->kind != Expr::NAME) return false;
        const Expr* returnp = nullptr;
        if (pushPureCall(expr, returnp)) {
            const bool folded = evalReal(*returnp, value);
            popPureCall();
            if (folded) return true;
        }
        if (expr.args.size() != 1) return false;
        const std::string& fn = expr.a->text;
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
bool Lowering::rejectRealObject(const VType& type, FileLine* flp, bool allowUnconstrained) {
    if (type.kind == VType::REAL) {
        unsupportedError(flp, "real variable, signal, or port");
        return true;
    }
    if (!allowUnconstrained && !type.dtp) {
        unsupportedError(flp, "object of an unconstrained array type");
        return true;
    }
    return false;
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
            const auto existing = m_enumConsts.find(decl.literals[i].first);
            if (existing != m_enumConsts.end() && existing->second->dtypep() != type.dtp) {
                m_ambiguousLiterals.insert(decl.literals[i].first);
            }
            m_enumConsts[decl.literals[i].first] = constp;
        }
        break;
    }
    case TypeDecl::ARRAY: {
        VType elem;
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
                return;
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
            return;
        }
        if (!decl.index.hasRange) {
            // An index that is a type mark takes the mark's range: an enumeration's
            // positions (IEEE 1800-2023 6.9), or a discrete subtype's own bounds
            const auto indexIt = m_types.find(decl.index.mark);
            const bool enumIndex = indexIt != m_types.end() && indexIt->second.kind == VType::ENUM
                                   && !indexIt->second.literals.empty();
            const bool intIndex = indexIt != m_types.end() && indexIt->second.kind == VType::INT;
            if (!enumIndex && !intIndex) {
                unsupportedError(flp, "array type without an index constraint");
                return;
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
                    = static_cast<int64_t>(m_types.find(specs[k]->mark)->second.literals.size())
                      - 1;
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
            // Every level survives, so a use supplies one index constraint per level
            VType inner = elem;
            for (size_t k = specs.size(); k-- > 0;) {
                VType arr;
                arr.kind = VType::ARRAY;
                arr.elemp = std::make_shared<VType>(inner);
                arr.elemWidth = inner.width;
                arr.kwd = inner.kwd;
                arr.signing = VSigning::NOSIGN;
                arr.width = 0;
                inner = arr;
            }
            type = inner;
            break;
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
        break;
    }
    case TypeDecl::RECORD: {
        int total = 0;
        VBasicDTypeKwd kwd = VBasicDTypeKwd::BIT;
        std::vector<VField> fields;
        for (const TypeDecl::Member& member : decl.members) {
            VType memberType;
            const std::string& mark = member.type.mark;
            const bool unconstrainedMember
                = (mark == "std_logic_vector" || mark == "std_ulogic_vector"
                   || mark == "bit_vector" || mark == "unsigned" || mark == "signed"
                   || mark == "u_unsigned" || mark == "u_signed")
                  && !member.type.hasRange && member.type.constraintMark.empty();
            if (unconstrainedMember) {
                // An unconstrained element: its width comes from a subtype or port binding
                memberType.kind = VType::VECTOR;
                memberType.kwd
                    = mark == "bit_vector" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC;
                memberType.signing = vectorSigning(mark);
                memberType.width = 0;
            } else if (!resolveType(member.type, memberType)) {
                return;
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
        break;
    }
    }
    if (decl.kind == TypeDecl::PHYSICAL) {
        type.typeName = m_unitName.empty() ? decl.name : m_unitName + "." + decl.name;
    } else if (decl.kind != TypeDecl::SUBTYPE && type.typeName.empty()) {
        type.typeName = m_unitName.empty() ? decl.name : m_unitName + "." + decl.name;
    }
    m_types[decl.name] = type;
    m_ambiguousTypes.erase(decl.name);
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
            || (!deferredNames.empty() && typeDeclMarksAny(typeDecl, deferredNames))) {
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
static VType cloneArrayChain(const VType& type) {
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

//! Constrain one record element to a fixed vector range, keeping its element keyword/signing
void Lowering::setRecordField(FileLine* flp, VField& field, int left, int right, bool ascending) {
    VType constrained;
    constrained.kind = VType::VECTOR;
    constrained.kwd = VBasicDTypeKwd::LOGIC;
    constrained.signing = VSigning::UNSIGNED;
    if (field.type) {
        constrained.kwd = field.type->kwd;
        constrained.signing = field.type->signing;
    }
    constrained.left = left;
    constrained.right = right;
    constrained.ascending = ascending;
    constrained.width = std::abs(left - right) + 1;
    constrained.dtp = findVector(flp, constrained.kwd, constrained.signing, constrained.left,
                                 constrained.right, constrained.ascending);
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
            flp->v3error("VHDL: record '" + spec.mark + "' has no element '" + member.name + "'");
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
        flp->v3error("VHDL: type name '" + mark + "' is ambiguous between imported packages");
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
            flp->v3error("VHDL: range constraint on scalar type '" + mark + "'");
            return false;
        }
        out.kind = mark == "bit" ? VType::BIT : VType::LOGIC;
        out.dtp = findBasic(flp, mark == "bit" ? VBasicDTypeKwd::BIT : VBasicDTypeKwd::LOGIC);
        out.typeName = mark == "bit" ? "bit" : (mark == "boolean" ? "boolean" : "std_ulogic");
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
        // is rejected where it appears -- see rejectRealObject.
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
    const VSigning signing = vectorSigning(mark);
    out = vectorType(flp, kwd, signing,
                     std::abs(static_cast<int>(left) - static_cast<int>(right)) + 1);
    out.left = static_cast<int>(left);
    out.right = static_cast<int>(right);
    out.ascending = ascending;
    out.dtp = findVector(flp, kwd, signing, out.left, out.right, out.ascending);
    out.typeName = mark;
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
    case Expr::PHYS_LIT: {
        int64_t scaled = 0;
        if (!physicalValue(*m_designp, expr, scaled)) return nullptr;
        return makeTimeConst(flp, scaled);
    }
    case Expr::REAL_LIT: unsupportedError(flp, "real value in this context"); return nullptr;
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
            // The aggregate's own choices say how wide it is, which is all an operand of `&`
            // needs; only an `others` association leaves the length open
            VType derived;
            if (!deriveAggregateType(expr, derived)) return nullptr;
            return lowerAggregate(expr, derived);
        }
        return lowerAggregate(expr, *context);
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
    case Expr::INDEX:
        if (expr.a && expr.a->kind == Expr::NAME) {
            if (expr.a->text == "rising_edge" || expr.a->text == "falling_edge") {
                return lowerEdgeCall(expr);
            }
            // A user subprogram shadows a standard-package function of the same name
            if (m_subprograms.count(expr.a->text)) return lowerFunctionCall(expr, context);
            if (isIntrinsic(expr.a->text)) return lowerIntrinsic(expr);
            if (isConversion(expr.a->text)) return lowerConversion(expr);
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
    if (bChar && !aChar) {
        lp = lowerExpr(*expr.a, nullptr);
        if (!lp) return nullptr;
        const VType lctx = typeOfDType(lp->dtypep());
        rp = lowerExpr(*expr.b, logicalOp ? nullptr : &lctx);
    } else if (aChar && !bChar) {
        rp = lowerExpr(*expr.b, nullptr);
        if (!rp) return nullptr;
        const VType rctx = typeOfDType(rp->dtypep());
        lp = lowerExpr(*expr.a, logicalOp ? nullptr : &rctx);
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
        lp = lowerExpr(*expr.a, nullptr);
        if (!lp) return nullptr;
        rp = lowerExpr(*expr.b, nullptr);
    }
    if (!rp) {
        lp->deleteTree();
        return nullptr;
    }
    VType lt = typeOfLowered(*expr.a, lp);
    VType rt = typeOfLowered(*expr.b, rp);
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
        }
        if (lt.isInt() || rt.isInt()) {
            unsupportedError(flp, "concatenation with an integer operand");
            lp->deleteTree();
            rp->deleteTree();
            return nullptr;
        }
        const VBasicDTypeKwd kwd = lt.isVector() ? lt.kwd : rt.kwd;
        const VSigning signing = lt.isVector() ? lt.signing : rt.signing;
        const VType resultType = vectorType(flp, kwd, signing, lt.width + rt.width);
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
        if (op == "rol" || op == "ror") return makeRotate(flp, lp, rp, op == "rol");
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
        if (!staticIntFold(expr.b, exponent) || exponent < 0 || exponent > 64) {
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
        resultp = edgeFill ? static_cast<AstNodeExpr*>(new AstShiftRS{flp, valuep, amountp, width})
                  : vt.isSigned()
                      ? static_cast<AstNodeExpr*>(new AstShiftRS{flp, valuep, amountp, width})
                      : static_cast<AstNodeExpr*>(new AstShiftR{flp, valuep, amountp, width});
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

AstNodeExpr* Lowering::lowerConversion(const Expr& expr) {
    FileLine* const flp = expr.flp;
    const std::string& name = expr.a->text;
    if (expr.args.size() == 2) {
        if (name == "to_unsigned" || name == "to_signed") {
            AstNodeExpr* valuep = lowerExpr(*expr.args[0], nullptr);
            if (!valuep) return nullptr;
            const VType valueType = typeOfDType(valuep->dtypep());
            int64_t width = 0;
            if (!staticIntFold(expr.args[1], width) || width <= 0 || width > 4096) {
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
    // model, which has no real type, and the conversion of a constant is a constant
    int64_t folded = 0;
    AstNodeExpr* valuep = evalInt(*expr.args[0], folded) ? makeIntConst(flp, folded)
                                                         : lowerExpr(*expr.args[0], nullptr);
    if (!valuep) return nullptr;
    const VType valueType = typeOfDType(valuep->dtypep());
    if (name == "to_integer" || name == "integer" || name == "natural" || name == "positive") {
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
                // An enumeration literal as an index is its position, which the literal
                // already carries (IEEE 1800-2023 6.9)
                const bool enumIndex = expr.args[ai]->kind == Expr::NAME
                                       && m_enumConsts.count(expr.args[ai]->text) != 0;
                if (!enumIndex) {
                    unsupportedError(flp, "non-integer index");
                    indexp->deleteTree();
                    curp->deleteTree();
                    return nullptr;
                }
                indexp = resizeExpr(indexp, intType(flp), flp);
                if (!indexp) {
                    curp->deleteTree();
                    return nullptr;
                }
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
    if (!typep && (expr.a->text == "character" || expr.a->text == "boolean")) {
        // `character` and `boolean` are predefined types the frontend models directly
        // rather than as entries in m_types; resolve them so their attributes reach below
        TypeSpec spec;
        spec.flp = flp;
        spec.mark = expr.a->text;
        if (resolveType(spec, resolved)) {
            typep = &resolved;
            typeMark = true;
        }
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
                AstNode* bodyp = new AstDisplay{flp,
                                                VDisplayType::DT_ERROR,
                                                "VHDL: '" + attribute + "' of the "
                                                    + (succ ? "last" : "first")
                                                    + " enumeration literal is outside the type",
                                                nullptr,
                                                nullptr,
                                                'd'};
                VTimescale unit = m_modp->timeunit();
                if (unit.isNone()) unit = VTimescale{VTimescale::TS_1PS};
                VN_AS(bodyp, Display)->fmtp()->timeunit(unit);
                bodyp = AstNode::addNext<AstNode, AstNode>(bodyp, new AstFinish{flp});
                m_modp->addStmtsp(
                    new AstAlways{flp, VAlwaysKwd::ALWAYS_COMB, nullptr,
                                  new AstIf{flp, atLimitp->cloneTree(false), bodyp, nullptr}});
            }
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
                text = condValue ? "TRUE" : "FALSE";
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

//! A conditional signal assignment with no final `else` is a process that keeps the
//! target value when no condition holds. Emit it as a latch, so the hold is explicit
//! rather than feeding the target back into a continuous assignment.
AstNode* Lowering::lowerLatchedAlternatives(const Concurrent& conc, const VType& type,
                                            AstNodeExpr* lhsp) {
    FileLine* const flp = conc.flp;
    AstNode* restp = nullptr;
    for (std::size_t i = conc.alternatives.size(); i-- > 0;) {
        const auto& alt = conc.alternatives[i];
        if (alt.first->kind == Expr::UNAFFECTED) {
            // The branch holds the target: an empty statement under its condition
            AstNode* const holdp = new AstComment{flp, "unaffected"};
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
        valuep = resizeExpr(valuep, type, flp);
        AstNode* const assignp = new AstAssign{flp, lhsp->cloneTree(false), valuep};
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
            if (!agg.choices[i].empty()) return false;
            include(0, static_cast<int64_t>(i));
        }
        return any;
    }
    for (size_t i = 0; i < agg.args.size(); ++i) {
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

bool Lowering::lowerChoiceConsts(const std::vector<ExprP>& choices, AstNodeDType* dtp,
                                 std::vector<AstConst*>& cops) {
    VType choiceType;
    const bool choiceEnum = enumTypeOfDType(dtp, choiceType);
    if (!choiceEnum) choiceType = typeOfDType(dtp);
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
        cops.push_back(constp);
    }
    return true;
}

//! Whether a lowered value contains a high-impedance constant.
static bool astHasZ(AstNode* nodep) {
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
        if (covered.insert(static_cast<int64_t>(cop->num().toUInt())).second) continue;
        cop->v3error(std::string{"VHDL: duplicate choice in a "} + what
                     + " (IEEE 1076-2008 10.9, 11.5)");
        ok = false;
    }
    return ok;
}

bool Lowering::choicesComplete(AstNodeDType* selDtp, const std::set<int64_t>& covered) const {
    VType enumType;
    if (enumTypeOfDType(selDtp, enumType)) {
        for (size_t i = 0; i < enumType.literals.size(); ++i) {
            if (!covered.count(static_cast<int64_t>(i))) return false;
        }
        return true;
    }
    const VType type = typeOfDType(selDtp);
    if (type.kind == VType::LOGIC || type.kind == VType::BIT) {
        return covered.count(0) && covered.count(1);
    }
    if (type.isVector() && type.width > 0 && type.width <= 16) {
        return covered.size() == (size_t{1} << type.width);
    }
    return false;
}
void Lowering::checkDrivers() {
    // A composite signal's scalar subelements are separate signals (IEEE 1076-2008
    // 12.6.4), so drivers are compared per bit position. Each emitted always/initial
    // statement and each bare continuous assignment is one driver; a write whose index
    // is not static is taken to cover the whole signal. A declared initial value is not
    // a driver.
    struct Range final {
        int lsb = 0;
        int width = 0;
        int driver = 0;
    };
    std::map<AstVar*, std::vector<Range>> ranges;
    int driver = 0;
    for (AstNode* stmtp = m_modp->stmtsp(); stmtp; stmtp = stmtp->nextp()) {
        if (VN_IS(stmtp, InitialStatic) || VN_IS(stmtp, Var) || VN_IS(stmtp, Cell)) continue;
        if (!VN_IS(stmtp, Always) && !VN_IS(stmtp, Initial) && !VN_IS(stmtp, AssignW)
            && !VN_IS(stmtp, AssignDly) && !VN_IS(stmtp, Assign)) {
            continue;
        }
        ++driver;
        stmtp->foreach([&](AstVarRef* refp) {
            if (refp->access() != VAccess::WRITE) return;
            AstVar* const varp = refp->varp();
            if (!m_unresolvedSignals.count(varp)) return;
            Range range;
            range.driver = driver;
            range.width = varp->width();
            // Compose the selects around the write, so a record element or array element
            // covers only its own bits; the indices are folded here rather than later
            const AstNode* nodep = refp;
            while (const AstSel* const selp = VN_CAST(nodep->backp(), Sel)) {
                if (selp->fromp() != nodep) break;
                AstConst* const lsbp = foldStatic(const_cast<AstNodeExpr*>(selp->lsbp()));
                if (!lsbp) break;
                range.lsb += static_cast<int>(lsbp->toUInt());
                lsbp->deleteTree();
                range.width = selp->widthConst();
                nodep = selp;
            }
            ranges[varp].push_back(range);
        });
    }
    for (const auto& entry : ranges) {
        const std::vector<Range>& vec = entry.second;
        bool reported = false;
        for (size_t i = 0; i < vec.size() && !reported; ++i) {
            for (size_t j = i + 1; j < vec.size(); ++j) {
                if (vec[i].driver == vec[j].driver) continue;
                if (vec[i].lsb >= vec[j].lsb + vec[j].width) continue;
                if (vec[j].lsb >= vec[i].lsb + vec[i].width) continue;
                entry.first->v3error("VHDL: unresolved signal '" + entry.first->name()
                                     + "' has more than one driver (IEEE 1076-2008 12.6.4)");
                reported = true;
                break;
            }
        }
    }
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
    // Lower every value once. Checking the lowered value also catches a high-impedance
    // constant produced by a folded call, not only a literal in the source.
    std::vector<AstNodeExpr*> values;
    values.reserve(conc.choices.size());
    bool tristate = false;
    for (const auto& alternative : conc.choices) {
        AstNodeExpr* valuep = lowerExpr(*alternative.second, &ltype);
        if (!valuep) {
            for (AstNodeExpr* const ownedp : values) ownedp->deleteTree();
            lhsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        valuep = resizeExpr(valuep, ltype, flp);
        if (astHasZ(valuep)) tristate = true;
        values.push_back(valuep);
    }
    const auto deleteValues = [&values](std::size_t from) {
        for (std::size_t i = from; i < values.size(); ++i) {
            if (values[i]) values[i]->deleteTree();
        }
    };
    // V3Tristate derives the output enable from a conditional expression, not from a
    // case item, so a selected assignment that releases the driver is lowered to the
    // equivalent if/elsif chain.
    if (tristate) {
        AstNodeExpr* restp = nullptr;
        std::set<int64_t> covered;
        bool bad = false;
        for (std::size_t i = conc.choices.size(); i-- > 0;) {
            AstNodeExpr* const valuep = values[i];
            values[i] = nullptr;
            std::vector<AstConst*> cops;
            if (!lowerChoiceConsts(conc.choices[i].first, selDtp, cops)) {
                for (AstConst* const cop : cops) cop->deleteTree();
                valuep->deleteTree();
                deleteValues(0);
                if (restp) restp->deleteTree();
                lhsp->deleteTree();
                selectorp->deleteTree();
                return nullptr;
            }
            if (cops.empty()) {  // The `others` alternative is the fall-through
                if (restp) {
                    // A second `others` can never be selected (IEEE 1076-2008 11.5)
                    flp->v3error("VHDL: more than one 'others' choice in a selected "
                                 "assignment");
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
                AstEq* const eqp = new AstEq{flp, selectorp->cloneTree(false), cop};
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
            condexprp->dtypep(ltype.dtp);
            restp = condexprp;
        }
        selectorp->deleteTree();
        if (!restp) {
            // A selected assignment without `others` must be exhaustive; the
            // fall-through is then unreachable, but a conditional expression needs one.
            if (bad || !choicesComplete(selDtp, covered)) {
                flp->v3error("VHDL: selected assignment does not cover every value of "
                             "its selector (IEEE 1076-2008 11.5)");
                lhsp->deleteTree();
                return nullptr;
            }
            restp = makeRepeatedConst(flp, ltype, '0');
        } else if (bad) {
            restp->deleteTree();
            lhsp->deleteTree();
            return nullptr;
        }
        // The caller wraps this in the conditional-assignment always
        return new AstAssignW{flp, lhsp, restp};
    }
    AstCaseItem* itemsp = nullptr;
    AstCaseItem* currentp = nullptr;
    std::set<int64_t> covered;
    bool bad = false;
    for (std::size_t i = 0; i < conc.choices.size(); ++i) {
        AstNodeExpr* const valuep = values[i];
        values[i] = nullptr;
        AstNode* const assignp = new AstAssignW{flp, lhsp->cloneTree(false), valuep};
        std::vector<AstConst*> cops;
        if (!lowerChoiceConsts(conc.choices[i].first, selDtp, cops)) {
            for (AstConst* const cop : cops) cop->deleteTree();
            assignp->deleteTree();
            deleteValues(i + 1);
            if (itemsp) itemsp->deleteTree();
            lhsp->deleteTree();
            selectorp->deleteTree();
            return nullptr;
        }
        if (cops.empty() && currentp) {
            // A second "others" alternative can never be selected (IEEE 1076-2008 11.5)
            flp->v3error("VHDL: more than one 'others' choice in a selected assignment");
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
    lhsp->deleteTree();
    if (!itemsp) {
        selectorp->deleteTree();
        return nullptr;
    }
    if (!bad && !currentp && !choicesComplete(selDtp, covered)) {
        flp->v3error("VHDL: selected assignment does not cover every value of its selector "
                     "(IEEE 1076-2008 11.5)");
        bad = true;
    }
    if (bad) {
        itemsp->deleteTree();
        selectorp->deleteTree();
        return nullptr;
    }
    return makeCase(flp, selectorp, itemsp);
}

AstNode* Lowering::lowerAggregateAssign(FileLine* flp, const Expr& target, const Expr& value,
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
    AstNode* headp = nullptr;
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
        AstNode* assignp = nullptr;
        if (variable) {
            assignp = new AstAssign{flp, lhsp, partp};
        } else if (concurrentProc) {
            assignp = new AstAssignW{flp, lhsp, partp};
        } else {
            assignp = new AstAssignDly{flp, lhsp, partp};
        }
        headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, assignp) : assignp;
    }
    valuep->deleteTree();
    return headp;
}

AstNode* Lowering::lowerAssign(const Stmt& stmt, bool concurrent, bool variable) {
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
    // A bare `x <= unaffected;` leaves the target unchanged, so emit nothing
    if (stmt.alternatives.empty() && stmt.value && stmt.value->kind == Expr::UNAFFECTED) {
        return nullptr;
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
        if (rhsp) rhsp = resizeExpr(rhsp, ltype, stmt.flp);
    }
    if (!rhsp) {
        lhsp->deleteTree();
        return nullptr;
    }
    // A concurrent procedure call drives its signal actuals as continuous assignments
    if (m_concurrentProc) {
        return new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                             new AstAssignW{stmt.flp, lhsp, rhsp}};
    }
    // Only variable assignments update immediately; a signal write waits for the
    // delta even in the time-zero activation (IEEE 1076-2008 10.5)
    if (variable) return new AstAssign{stmt.flp, lhsp, rhsp};
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

AstNode* Lowering::lowerIf(const Stmt& stmt) {
    AstNode* elsesp = nullptr;
    // A value tracked before the statement still holds inside each branch, which are
    // lowered in reverse, so each one starts from the same values
    const auto entryVals = saveVarVals();
    for (std::size_t i = stmt.branches.size(); i-- > 0;) {
        const auto& branch = stmt.branches[i];
        restoreVarVals(entryVals);
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
    if (stmt.kind == Stmt::REPORT) return lowerReport(stmt);
    if (stmt.kind == Stmt::ASSERT) return lowerAssert(stmt);
    if (stmt.kind == Stmt::CALL) {
        // `std.env.finish` ends the simulation
        if (stmt.value && stmt.value->kind == Expr::SELECTED && stmt.value->text == "finish"
            && !m_subprograms.count("finish")) {
            return new AstFinish{stmt.flp};
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
        // 1076-2008 8.1); any other wait inside a body has no model here
        if (stmt.waitForever && m_suspendFlagp) {
            return new AstAssign{stmt.flp, new AstVarRef{stmt.flp, m_suspendFlagp, VAccess::WRITE},
                                 bitConst(stmt.flp, '1')};
        }
        unsupportedError(stmt.flp, "wait statement inside a process body");
        return nullptr;
    }
    // exit/next are handled by lowerStmtList, which owns their flags
    return nullptr;
}

AstNode* Lowering::lowerCase(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    // As in an if, an alternative starts from the values that held before the statement
    const auto entryVals = saveVarVals();
    AstNodeExpr* const selectorp = lowerExpr(*stmt.selector, nullptr);
    if (!selectorp) return nullptr;
    AstNodeDType* const selDtp = selectorp->dtypep();
    AstCaseItem* itemsp = nullptr;
    AstCaseItem* currentp = nullptr;  // The "others" alternative, if seen
    std::set<int64_t> covered;
    bool bad = false;
    for (const auto& item : stmt.caseItems) {
        restoreVarVals(entryVals);
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
            // A second "others" alternative can never be selected (IEEE 1076-2008 10.9)
            flp->v3error("VHDL: more than one 'others' choice in a case statement");
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
    if (!bad && !currentp && !choicesComplete(selDtp, covered)) {
        flp->v3error("VHDL: case statement does not cover every value of its selector "
                     "(IEEE 1076-2008 10.9)");
        bad = true;
    }
    if (bad) {
        itemsp->deleteTree();
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
        updateVarVals(*stmtp);
        if (!nodep) continue;
        // Statements after a conditional exit/next only run when it was not taken
        if (runCondp) nodep = new AstIf{stmtp->flp, runCondp->cloneTree(false), nodep, nullptr};
        if (AstNodeExpr* const guardp = loopGuard(stmtp->flp)) {
            nodep = new AstIf{stmtp->flp, guardp, nodep, nullptr};
        }
        headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
        // A wait suspends the process at once: the rest of this list never runs
        if (stmtp->kind == Stmt::WAIT) break;
        // Severity failure ends the run at the statement (IEEE 1076-2008 8.3):
        // nothing after an unconditional report runs, and the statements after a
        // failing assert run only when it did not fail
        if (stmtp->severity != Stmt::SEV_FAILURE) continue;
        if (stmtp->kind == Stmt::REPORT) break;
        if (stmtp->kind != Stmt::ASSERT) continue;
        const AstIf* const assertp = VN_CAST(nodep, If);
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
//! severity failure (IEEE 1076-2008 8.3)
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
        // A severity-failure report or assert, when the process reaches it, ends
        // the run (IEEE 1076-2008 8.3), which is a valid process stopper: the body
        // runs once, at time zero, and the run ends at the failure
        if (!usesSeverityFailure(process.body)) {
            unsupportedError(flp, "process without a sensitivity list or wait statement");
            return;
        }
        m_initial = true;
        m_varVals.clear();
        AstNode* const initp = lowerStmtList(process.body);
        m_initial = false;
        if (initp) m_modp->addStmtsp(new AstInitialStatic{flp, initp});
        restoreScope(saved);
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
    // A bare `wait;` in the body, other than the process's own first wait, suspends
    // the process when reached (IEEE 1076-2008 8.1): a flag gates every later
    // activation, and the reached wait sets it
    m_suspendFlagp = nullptr;
    const bool bareWaitInBody = !waitp->waitForever && usesBareWait(bodyStmts);
    const auto makeSuspendFlag = [&]() {
        m_suspendFlagp
            = new AstVar{flp, VVarType::VAR, "vhdlwait_" + std::to_string(++m_loopSeq) + "__done",
                         findBasic(flp, VBasicDTypeKwd::LOGIC)};
        m_modp->addStmtsp(m_suspendFlagp);
    };
    if (waitp->delay) {
        if (bareWaitInBody) makeSuspendFlag();
        // A timed wait suspends the process until the delay expires and the process then
        // runs again from the top, so its whole body repeats
        AstNode* bodyp = lowerStmtRange(process.body, 0, waitIndex);
        AstNode* const delayp = lowerDelay(*waitp->delay, flp);
        if (!delayp) {
            m_suspendFlagp = nullptr;
            restoreScope(saved);
            return;
        }
        bodyp = bodyp ? AstNode::addNext<AstNode, AstNode>(bodyp, delayp) : delayp;
        if (AstNode* const postp
            = lowerStmtRange(process.body, waitIndex + 1, process.body.size())) {
            bodyp = AstNode::addNext<AstNode, AstNode>(bodyp, postp);
        }
        AstLoop* const loopp = new AstLoop{flp};
        // The repeated body stops for good when it reaches a bare wait
        AstNodeExpr* runp = new AstConst{flp, 1};
        if (m_suspendFlagp) {
            runp = new AstNot{flp, new AstVarRef{flp, m_suspendFlagp, VAccess::READ}};
            runp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
        }
        loopp->addStmtsp(new AstLoopTest{flp, loopp, runp});
        if (bodyp) loopp->addStmtsp(bodyp);
        m_modp->addStmtsp(new AstInitial{flp, loopp});
        m_suspendFlagp = nullptr;
        restoreScope(saved);
        return;
    }
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
    if (bareWaitInBody && !singleShot) makeSuspendFlag();
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
    if (bodyp) {
        m_modp->addStmtsp(
            new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemsp}, bodyp});
    } else {
        itemsp->deleteTree();
    }
    m_suspendFlagp = nullptr;
    restoreScope(saved);
}

AstNode* Lowering::lowerFor(const Stmt& stmt) {
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
        m_varVals.clear();
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

AstNode* Lowering::lowerForRuntime(const Stmt& stmt) {
    FileLine* const flp = stmt.flp;
    // `exit` and `next` act on an iteration the unroller cannot see
    if (usesExitNext(stmt.body)) {
        unsupportedError(flp, "loop with 'exit' or 'next' whose range is not static");
        return nullptr;
    }
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
        = new AstVar{flp, VVarType::VAR, "vhdlfor_" + std::to_string(++m_loopSeq), intT.dtp};
    m_modp->addStmtsp(varp);
    const ScopeSave savedScope = saveScope();
    m_scope.erase(stmt.loopVar);
    m_scope[stmt.loopVar] = VarInfo{varp, intT};
    // The discrete range is evaluated once, at loop entry (IEEE 1076-2008 8.9), so a
    // bound the body writes must not re-read on every test: capture it first
    AstNode* capturep = nullptr;
    if (!VN_CAST(boundp, Const)) {
        AstVar* const boundVarp
            = new AstVar{flp, VVarType::VAR, "vhdlbound_" + std::to_string(++m_loopSeq), intT.dtp};
        m_modp->addStmtsp(boundVarp);
        capturep = new AstAssign{flp, new AstVarRef{flp, boundVarp, VAccess::WRITE}, boundp};
        boundp = new AstVarRef{flp, boundVarp, VAccess::READ};
    }
    AstLoop* const loopp = new AstLoop{flp};
    AstNodeExpr* const testp
        = up ? static_cast<AstNodeExpr*>(
                   new AstLte{flp, new AstVarRef{flp, varp, VAccess::READ}, boundp})
             : static_cast<AstNodeExpr*>(
                   new AstGte{flp, new AstVarRef{flp, varp, VAccess::READ}, boundp});
    testp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    loopp->addStmtsp(new AstLoopTest{flp, loopp, testp});
    if (AstNode* const bodyp = lowerStmtList(stmt.body)) loopp->addStmtsp(bodyp);
    AstNodeExpr* const stepp
        = new AstAdd{flp, new AstVarRef{flp, varp, VAccess::READ}, new AstConst{flp, 1}};
    stepp->dtypep(intT.dtp);
    loopp->addStmtsp(new AstAssign{flp, new AstVarRef{flp, varp, VAccess::WRITE}, stepp});
    restoreScope(savedScope);
    AstNode* resultp = AstNode::addNext<AstNode, AstNode>(
        new AstAssign{flp, new AstVarRef{flp, varp, VAccess::WRITE}, initp}, loopp);
    if (capturep) resultp = AstNode::addNext<AstNode, AstNode>(capturep, resultp);
    return resultp;
}

//! The text of a literal report message, or a default when there is none
static std::string reportText(const Expr* exprp, const char* fallback) {
    if (!exprp) return fallback;
    if (exprp->kind != Expr::STRING_LIT) return fallback;
    std::string text = exprp->spelling;
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

AstNode* Lowering::lowerReport(const Stmt& stmt) {
    // A computed message is not modelled yet: only a literal reaches the model
    if (stmt.value && stmt.value->kind != Expr::STRING_LIT) return nullptr;
    AstNode* const displayp = new AstDisplay{
        stmt.flp, VDisplayType::DT_DISPLAY, reportText(stmt.value.get(), ""), nullptr, nullptr,
        'd'};
    // `report ... severity failure` ends the simulation (IEEE 1076-2008 8.3); the other
    // severities only report
    if (stmt.severity == Stmt::SEV_FAILURE) {
        return AstNode::addNext<AstNode, AstNode>(displayp, new AstFinish{stmt.flp});
    }
    return displayp;
}

AstNode* Lowering::lowerAssert(const Stmt& stmt) {
    AstNode* const bodyp = assertBody(stmt.flp, *stmt.value, stmt.message.get(), stmt.severity);
    if (bodyp && m_concurrentProc) {
        // A concurrent procedure call has no enclosing process; the assertion is
        // evaluated continuously, like a concurrent assertion
        return new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS_COMB, nullptr, bodyp};
    }
    return bodyp;
}

AstNode* Lowering::assertBody(FileLine* flp, const Expr& condition, const Expr* message,
                              Stmt::Severity severity) {
    AstNodeExpr* const condp = lowerCond(condition);
    if (!condp) return nullptr;
    // A failing assertion reports, and `severity failure` ends the run (IEEE 1076-2008 8.3)
    AstNodeExpr* const failp = new AstNot{flp, condp};
    failp->dtypep(findBasic(flp, VBasicDTypeKwd::LOGIC));
    AstNode* bodyp = new AstDisplay{
        flp, VDisplayType::DT_DISPLAY, reportText(message, "Assertion failed"), nullptr, nullptr,
        'd'};
    if (severity == Stmt::SEV_FAILURE) {
        bodyp = AstNode::addNext<AstNode, AstNode>(bodyp, new AstFinish{flp});
    }
    return new AstIf{flp, failp, bodyp, nullptr};
}

AstNode* Lowering::lowerDelay(const Expr& expr, FileLine* flp) {
    int64_t fs = 0;
    if (!evalInt(expr, fs)) {
        unsupportedError(flp, "delay that is not static");
        return nullptr;
    }
    VTimescale unit = m_modp->timeunit();
    if (unit.isNone()) unit = VTimescale{VTimescale::TS_1PS};
    const double seconds = unit.multiplier();
    if (seconds <= 0) {
        unsupportedError(flp, "delay without a timescale");
        return nullptr;
    }
    AstDelay* const delayp = new AstDelay{
        flp, new AstConst{flp, static_cast<uint32_t>(static_cast<double>(fs) * 1e-15 / seconds)},
        false};
    delayp->timeunit(unit);
    return delayp;
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
    m_ambiguousTypes = saved.ambiguousTypes;
    m_ambiguousLiterals = saved.ambiguousLiterals;
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

//! Whether a subprogram opens a file to build its result while the design is elaborated
static bool readsFile(const SubprogramDecl& subp) {
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
    std::vector<const SubprogramRef*> matching;
    for (const SubprogramRef* const refp : refs) {
        const SubprogramDecl& sub = *refp->declp;
        if ((sub.kind == SubprogramDecl::FUNCTION) != function) continue;
        if (!sub.hasBody) {
            withoutBody = true;
            continue;
        }
        if (function && sub.impure) {
            // An impure function that reads a file builds its result at elaboration,
            // so its call resolves like any other
            if (!readsFile(sub)) {
                impureSeen = true;
                continue;
            }
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
        // which one is called (IEEE 1800-2023 6.5.6.2). A single declaration is not a
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
                call.flp->v3error("VHDL: ambiguous call to '" + name + "'");
                return nullptr;
            }
        }
        matching = std::move(fitting);
    }
    if (!matching.empty()) {
        if (foundpp) *foundpp = matching[0];
        return matching[0]->declp;
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

//! A vector or an array: one category for overload matching, as only the element type
//! separates them and that is not compared here
static bool isVectorKind(const VType::Kind kind) {
    return kind == VType::VECTOR || kind == VType::ARRAY;
}

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
    case Expr::UNARY: return expr.a && actualKind(*expr.a, out);
    case Expr::NAME:
    case Expr::SELECTED:
    case Expr::INDEX:
    case Expr::SLICE: {
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
    case Expr::ATTR:
        if (expr.text == "high" || expr.text == "low" || expr.text == "left"
            || expr.text == "right" || expr.text == "length" || expr.text == "pos"
            || expr.text == "to_integer") {
            out = VType::INT;
            return true;
        }
        return false;
    case Expr::BINARY:
        if (expr.text == "=" || expr.text == "/=" || expr.text == "<" || expr.text == ">"
            || expr.text == "<=" || expr.text == ">=") {
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
//! category (IEEE 1800-2023 6.5.6.2). The frontend models no conversion except the numeric
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
    case Expr::UNARY:
        if (!visibleSubprograms(expr.text).empty()) return false;
        return expr.a && actualType(*expr.a, out);
    case Expr::BINARY: {
        if (!visibleSubprograms(expr.text).empty()) return false;
        const std::string& op = expr.text;
        if (op == "=" || op == "/=" || op == "<" || op == "<=" || op == ">" || op == ">="
            || op == "?=" || op == "?/=" || op == "?<" || op == "?<=" || op == "?>"
            || op == "?>=") {
            out = VType{};
            out.kind = VType::LOGIC;
            out.typeName = "boolean";
            return true;
        }
        VType aType;
        VType bType;
        if (!actualType(*expr.a, aType) || !actualType(*expr.b, bType)) return false;
        if (op == "and" || op == "or" || op == "xor" || op == "nand" || op == "nor"
            || op == "xnor") {
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
        if (op == "sll" || op == "srl" || op == "sla" || op == "sra" || op == "rol"
            || op == "ror") {
            if (!aType.isVector() && !bType.isVector()) return false;
            out = aType.isVector() ? aType : bType;
            return true;
        }
        if (aType.kind == VType::INT && bType.kind == VType::INT) {
            out = intType(expr.flp);
            return true;
        }
        if (aType.kind == VType::REAL || bType.kind == VType::REAL) {
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
    case Expr::QUAL: {
        TypeSpec spec;
        spec.flp = expr.flp;
        spec.mark = expr.text;
        return formalType(spec, out);
    }
    case Expr::NAME:
    case Expr::SELECTED:
    case Expr::SLICE: {
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
        if (m_realConsts.count(expr.text)) {
            out = VType{};
            out.kind = VType::REAL;
            out.typeName = "real";
            return true;
        }
        return false;
    }
    case Expr::INDEX: {
        if (typeOfDesignator(expr, out)) return true;
        // The parser writes both an array index and a function call as INDEX; a call
        // has a subprogram name as its prefix.
        if (!expr.a || expr.a->kind != Expr::NAME || !m_subprograms.count(expr.a->text)) {
            return false;
        }
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
    case Expr::ATTR:
        if (expr.text == "high" || expr.text == "low" || expr.text == "left"
            || expr.text == "right" || expr.text == "length" || expr.text == "pos"
            || expr.text == "to_integer") {
            out = VType{};
            out.kind = VType::INT;
            out.typeName = "integer";
            return true;
        }
        return false;
    case Expr::CALL: {
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
    default: return false;
    }
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
        out.typeName = mark == "bit" ? "bit" : (mark == "boolean" ? "boolean" : "std_ulogic");
        return true;
    }
    if (mark == "character") {
        out = vectorType(spec.flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, 8);
        out.typeName = "character";
        return true;
    }
    if (mark == "string") {
        out.kind = VType::ARRAY;
        out.elemp = std::make_shared<VType>(
            vectorType(spec.flp, VBasicDTypeKwd::LOGIC, VSigning::UNSIGNED, 8));
        out.elemp->typeName = "character";
        out.elemWidth = 8;
        out.kwd = VBasicDTypeKwd::LOGIC;
        out.signing = VSigning::UNSIGNED;
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
    if (!formal.typeName.empty() && !actual.typeName.empty()
        && formal.typeName != actual.typeName) {
        return false;
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
            if (!typesMatch(fType, aType)) return false;
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
AstNodeExpr* Lowering::lowerUserOperator(const Expr& expr, const VType* context, bool& found) {
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
        if (context) {
            VType returnType;
            if (formalType(subp->returnType, returnType) && !typesMatch(returnType, *context)) {
                continue;
            }
        }
        matches.push_back(subp);
    }
    if (matches.empty()) return nullptr;
    found = true;
    if (matches.size() > 1) {
        expr.flp->v3error("VHDL: operator '" + expr.text + "' is ambiguous for these operands");
        return nullptr;
    }
    Expr call{Expr::CALL};
    call.flp = expr.flp;
    call.text = expr.text;
    call.spelling = expr.spelling;
    call.args.push_back(cloneExpr(*expr.a));
    call.args.push_back(cloneExpr(*expr.b));
    return lowerFunctionCall(call, context, matches.front());
}

//! A user-defined prefix operator whose formal exactly matches the operand
AstNodeExpr* Lowering::lowerUserUnary(const Expr& expr, const VType* context, bool& found) {
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
        if (context) {
            VType returnType;
            if (formalType(subp->returnType, returnType) && !typesMatch(returnType, *context)) {
                continue;
            }
        }
        matches.push_back(subp);
    }
    if (matches.empty()) return nullptr;
    found = true;
    if (matches.size() > 1) {
        expr.flp->v3error("VHDL: operator '" + expr.text + "' is ambiguous for this operand");
        return nullptr;
    }
    Expr call{Expr::CALL};
    call.flp = expr.flp;
    call.text = expr.text;
    call.spelling = expr.spelling;
    call.args.push_back(cloneExpr(*expr.a));
    return lowerFunctionCall(call, context, matches.front());
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
        const VType* ctxp = nullptr;
        const std::string& formalMark = formalp->type.mark;
        const bool predefinedUnconstrained
            = !formalp->type.hasRange && formalp->type.constraintMark.empty()
              && formalp->type.indexConstraints.empty()
              && (formalMark == "std_logic_vector" || formalMark == "std_ulogic_vector"
                  || formalMark == "bit_vector" || formalMark == "unsigned"
                  || formalMark == "signed" || formalMark == "u_unsigned"
                  || formalMark == "u_signed");
        if (!predefinedUnconstrained && resolveType(formalp->type, formalType)
            && (formalType.isVector() ? formalType.width > 0 : formalType.dtp != nullptr)) {
            ctxp = &formalType;
        }
        AstNodeExpr* actualp = asLvalue ? lowerDesignator(actual, true) : lowerExpr(actual, ctxp);
        if (!actualp) return false;
        InlineBinding binding{actualp, asLvalue, true};
        // The actual's VHDL type first: an array's element width and index bounds are gone
        // from its data type, and a formal indexing the actual needs both
        if (!typeOfDesignator(actual, binding.type)) {
            if (ctxp) {
                binding.type = *ctxp;
            } else {
                binding.type = typeOfDType(actualp->dtypep());
            }
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

//! `read`/`hread` take one value of `width` bits from the front of a line and leave
//! the rest of it unread, which is what GHDL and nvc both do
static bool fileReadValue(const std::string& line, size_t& pos, int width, bool hex,
                          std::string& bits) {
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

AstNodeExpr* Lowering::lowerFunctionCall(const Expr& call, const VType* context,
                                         const SubprogramDecl* forcedp) {
    const SubprogramRef* refp = nullptr;
    const SubprogramDecl* subp = nullptr;
    if (forcedp) {
        subp = forcedp;
        for (const auto& entry : m_subprograms) {
            for (const SubprogramRef& ref : entry.second) {
                if (ref.declp == forcedp) {
                    refp = &ref;
                    break;
                }
            }
            if (refp) break;
        }
        if (!refp) {
            call.flp->v3error("VHDL: operator '" + call.text + "' is no longer visible");
            return nullptr;
        }
    } else {
        subp = resolveCall(call, true, &refp);
        if (!subp) return nullptr;  // resolveCall reported why
    }
    // A subprogram that reads a file builds its result at elaboration
    if (readsFile(*subp)) return evalFileInit(*subp, call);
    m_funcWriteCost = 0;  // This call's own fold budget
    InlineEnv env;
    AstNode* unusedPrologsp = nullptr;  // A function has no out mode formal
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
    // An alias denotes its designator; its subtype mark gives it bounds of its own. An
    // alias with a signature renames a subprogram, so it is registered as one instead of
    // being valued like an object alias would be
    for (const AliasDecl& alias : subp->aliases) {
        if (!alias.designator) continue;
        if (alias.hasSignature) {
            registerSubprogramAlias(alias);
            continue;
        }
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
    // A constrained array return type keeps the element structure that the packed dtype
    // would otherwise lose; comparisons of a direct call need it. An unconstrained return
    // takes the element type from the declaration and the element count from the result.
    if (resultp) {
        VType structType;
        bool haveStruct = retType.kind == VType::ARRAY && retType.elemWidth > 0;
        if (haveStruct) {
            structType = retType;
        } else {
            const auto typeIt = m_types.find(subp->returnType.mark);
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
        // A conditional waveform cannot be folded to a single value here; lower it at run time
        if ((stmt.kind == Stmt::ASSIGN || stmt.kind == Stmt::VAR_ASSIGN)
            && !stmt.alternatives.empty()) {
            return nullptr;
        }
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
                return nullptr;
            }
            const auto it = activeEnv().find(basep->text);
            if (it == activeEnv().end()) {
                unsupportedError(flp, "assignment to an element outside the inlined subprogram");
                return nullptr;
            }
            // Each element write rebuilds the local's whole value, so the fold is bounded by
            // what it copies: a wide value costs its width per write, and the rebuilt tree
            // grows with every write
            m_funcWriteCost += 64 + it->second.exprp->width() / 64;
            if (m_funcWriteCost > (1 << 18)) {
                unsupportedError(flp, "function body with too many element assignments");
                return nullptr;
            }
            AstNodeExpr* const readp = lowerDesignator(target, false);
            AstSel* const selp = VN_CAST(readp, Sel);
            if (!selp) {
                if (readp) readp->deleteTree();
                unsupportedError(flp, "assignment to an element of a non-vector value");
                return nullptr;
            }
            // A nested target selects inside a select, so its offset in the local's value
            // is the sum of the levels: `v(i)(j)` lands inside `v`. The selects that came
            // from binding the local to its actual are not part of the target -- the
            // local's own value is what is rebuilt -- so summing stops at the level whose
            // width is the local's
            const int localWidth = it->second.exprp->width();
            AstNodeExpr* offsetp = selp->lsbp()->cloneTree(false);
            for (AstSel* subjectp = VN_CAST(selp->fromp(), Sel);
                 subjectp && subjectp->width() != localWidth;
                 subjectp = VN_CAST(subjectp->fromp(), Sel)) {
                AstNodeExpr* const summed
                    = new AstAdd{flp, offsetp, subjectp->lsbp()->cloneTree(false)};
                summed->dtypep(findBasic(flp, VBasicDTypeKwd::INT));
                offsetp = summed;
            }
            const VType elemType = typeOfDType(selp->dtypep());
            AstNodeExpr* valuep = lowerExpr(*stmt.value, &elemType);
            if (!valuep) {
                readp->deleteTree();
                offsetp->deleteTree();
                return nullptr;
            }
            AstNodeExpr* const updatedp
                = updateLocalElement(flp, it->second.exprp->cloneTree(false), valuep,
                                     selp->dtypep(), offsetp, selp->widthConst());
            readp->deleteTree();
            it->second.exprp->deleteTree();
            it->second.exprp = updatedp;
            continue;
        }
        if (stmt.kind == Stmt::REPORT || stmt.kind == Stmt::ASSERT) continue;
        if (stmt.kind == Stmt::RETURN) {
            if (!stmt.value) {
                unsupportedError(flp, "return without a value in a VHDL function body");
                return nullptr;
            }
            return lowerExpr(*stmt.value, m_funcRetp);
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
    AstNodeExpr* thenp = evalFuncBlock(branch.second, 0, thenEnv, cont, exitContp, depth);
    AstNodeExpr* elsep = branchIndex + 1 < stmt.branches.size()
                             ? evalFuncBranches(stmt, branchIndex + 1, cont, exitContp, depth)
                             : evalFuncBlock({}, 0, activeEnv(), cont, exitContp, depth);
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
    // A selector that folds to a constant picks which arm runs, so the others need no value
    AstConst* const selConstp = foldStatic(selectorp);
    const bool known = selConstp && !selConstp->num().isFourState();
    bool kept = false;
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
            if (selConstp) selConstp->deleteTree();
            cleanup();
            selectorp->deleteTree();
            return nullptr;
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
        branch.resultp = evalFuncBlock(item.second, 0, branch.env, cont, exitContp, depth);
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
    } else if (!staticInt(stmt.b, left) || !staticInt(stmt.c, right)) {
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
        if (std::llabs(value - left) >= 4096) {
            unsupportedError(flp, "function loop with more than 4096 iterations");
            return nullptr;
        }
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
        if (iteration >= 4096) {
            unsupportedError(flp, "function while loop with more than 4096 iterations");
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
    m_funcWriteCost = 0;  // This call's own fold budget
    InlineEnv env;
    AstNode* prologsp = nullptr;  // Out-parameter initializations
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
    registerSubprograms(subp->subprograms, static_cast<int>(m_inlineFrames.size() - 1));
    for (const AliasDecl& alias : subp->aliases) registerAlias(alias);

    // Locals are real module variables reinitialized on every call, like automatics
    AstNode* initsp = nullptr;
    for (const Decl& decl : subp->decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType ltype;
        if (!resolveType(decl.type, ltype)) return fail(nullptr);
        if (rejectRealObject(ltype, decl.flp)) return fail(nullptr);
        for (const auto& name : decl.names) {
            const std::string mangled
                = "vhdlproc_" + std::to_string(++m_procVarSeq) + "__" + name.first;
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
    m_varVals.clear();
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
    m_varVals.clear();
    FileLine* const flp = stmt.flp;
    const auto& branch = stmt.branches[branchIndex];
    AstNodeExpr* const condp = branch.first ? lowerCond(*branch.first) : nullptr;
    if (branch.first && !condp) {
        fallsThroughp = false;
        return nullptr;
    }
    bool thenFalls = true;
    AstNode* const thensp = lowerProcChainWithTail(branch.second, tail, thenFalls);
    if (!condp) {
        // The final else has no condition, so its statements are appended as they are
        fallsThroughp = thenFalls;
        return thensp;
    }
    bool elseFalls = true;
    AstNode* elsesp = nullptr;
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
AstNode* Lowering::lowerProcCase(const Stmt& stmt, const std::vector<const Stmt*>& tail,
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
    m_edgeAsLevel = false;
    m_edgeAsPrev = false;
    m_edgePrevVars.clear();
    // Tracked variable values hold only within one process
    struct VarValsScope final {
        std::map<std::string, ExprP>& vals;
        explicit VarValsScope(std::map<std::string, ExprP>& vals_)
            : vals{vals_} {
            vals.clear();
        }
        ~VarValsScope() { vals.clear(); }
    } varValsScope{m_varVals};
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
        Expr nameExpr{Expr::NAME};
        nameExpr.flp = process.flp;
        nameExpr.text = name;
        nameExpr.spelling = name;
        AstVar* const varp = lookupVar(nameExpr);
        if (!varp) {
            // process(all) collects read names including subprogram and type marks; only
            // names that are actual signals sensitize the process
            if (process.sensitivityAll) continue;
            process.flp->v3error("VHDL: unknown signal '" + name + "' in a sensitivity list");
            return;
        }
        sensVars.push_back(varp);
    }
    // A process runs on every event of its sensitivity list, so an edge call must not
    // become a constant. A whole-signal edge is the signal's level when it is the only
    // sensitivity; a signal-element event and multi-sensitivity edges compare with the
    // previous value, which needs the previous-value variables maintained below.
    const bool hasEdges = stmtsContainEdge(process.body);
    m_edgeAsLevel = hasEdges && sensVars.size() == 1;
    m_edgeAsPrev = hasEdges && sensVars.size() > 1;
    if (lowerFlopIdiom(process)) {
        restoreScope(saved);
        return;
    }

    m_edges.clear();
    m_initial = false;
    m_varVals.clear();
    AstNode* bodyp = lowerStmtList(process.body);
    const std::vector<Edge> edges = m_edges;
    m_edges.clear();
    // Record the current signal values for the next run's edge detection
    AstNode* initPrevsp = nullptr;
    for (const auto& entry : m_edgePrevVars) {
        AstNodeExpr* const lhsp = new AstVarRef{process.flp, entry.second, VAccess::WRITE};
        AstNodeExpr* const rhsp = new AstVarRef{process.flp, entry.first, VAccess::READ};
        initPrevsp = initPrevsp ? AstNode::addNext<AstNode, AstNode>(
                                      initPrevsp, new AstAssign{process.flp, lhsp, rhsp})
                                : new AstAssign{process.flp, lhsp, rhsp};
    }
    if (!m_edgePrevVars.empty() && bodyp && initPrevsp) {
        bodyp = AstNode::addNext(bodyp, initPrevsp->cloneTree(true));
    }
    m_initial = true;
    m_varVals.clear();
    AstNode* initp = lowerStmtList(process.body);
    m_initial = false;
    if (!m_edgePrevVars.empty() && initp && initPrevsp) {
        initp = AstNode::addNext(initp, initPrevsp);
        initPrevsp = nullptr;
    }
    if (initPrevsp) initPrevsp->deleteTree();

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
        if (!m_edgeAsLevel && !m_edgeAsPrev) {
            for (const Edge& edge : edges) {
                if (edge.varp == varp) type = edge.type;
            }
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

//! The asynchronous-reset flip-flop idiom a process writes:
//! `if rst = <active> then <reset values> elsif rising_edge(clk) then <clocked> end if`.
//! Emitting it as an edge-triggered always puts the clock in the block's sensitivity,
//! where the scheduler keeps it; the level test inside the body used to be the only place
//! the clock appeared, and a split of the body could drop it.
bool Lowering::lowerFlopIdiom(const Process& process) {
    if (process.body.size() != 1 || !process.body[0]) return false;
    const Stmt& ifStmt = *process.body[0];
    if (ifStmt.kind != Stmt::IF || ifStmt.branches.size() != 2) return false;
    // The test and the edge are usually written in parentheses: `if (rst = '0') then`
    const auto unwrap = [](const Expr* exprp) {
        while (exprp && exprp->kind == Expr::PAREN && exprp->a) exprp = exprp->a.get();
        return exprp;
    };
    const Expr* const testp = unwrap(ifStmt.branches[0].first.get());  // `rst = '0'`
    const Expr* const edgep = unwrap(ifStmt.branches[1].first.get());  // `rising_edge(clk)`
    if (!testp || !edgep) return false;
    if (edgep->kind != Expr::INDEX || !edgep->a || edgep->a->kind != Expr::NAME
        || edgep->args.size() != 1 || !edgep->args[0] || edgep->args[0]->kind != Expr::NAME) {
        return false;
    }
    const bool rising = edgep->a->text == "rising_edge";
    if (!rising && edgep->a->text != "falling_edge") return false;
    if (testp->kind != Expr::BINARY || (testp->text != "=" && testp->text != "/=") || !testp->a
        || !testp->b) {
        return false;
    }
    const Expr* namep = nullptr;
    const Expr* levelp = nullptr;
    if (unwrap(testp->a.get())->kind == Expr::NAME) {
        namep = unwrap(testp->a.get());
        levelp = unwrap(testp->b.get());
    } else if (unwrap(testp->b.get())->kind == Expr::NAME) {
        namep = unwrap(testp->b.get());
        levelp = unwrap(testp->a.get());
    } else {
        return false;
    }
    if (levelp->kind != Expr::CHAR_LIT || (levelp->charValue != '0' && levelp->charValue != '1')) {
        return false;
    }
    // The process must be sensitive to exactly the reset and the clock, so that those two
    // edges are all it can be woken by
    const std::string& resetName = namep->text;
    const std::string& clockName = edgep->args[0]->text;
    if (process.sensitivity.size() != 2 || resetName == clockName
        || std::find(process.sensitivity.begin(), process.sensitivity.end(), resetName)
               == process.sensitivity.end()
        || std::find(process.sensitivity.begin(), process.sensitivity.end(), clockName)
               == process.sensitivity.end()) {
        return false;
    }
    Expr resetNameExpr{Expr::NAME};
    resetNameExpr.flp = process.flp;
    resetNameExpr.text = resetName;
    resetNameExpr.spelling = namep->spelling;
    AstVar* const resetVarp = lookupVar(resetNameExpr);
    Expr clockNameExpr{Expr::NAME};
    clockNameExpr.flp = process.flp;
    clockNameExpr.text = clockName;
    clockNameExpr.spelling = edgep->args[0]->spelling;
    AstVar* const clockVarp = lookupVar(clockNameExpr);
    if (!resetVarp || !clockVarp) return false;
    // `rst = '0'` and `rst /= '1'` test the active level, `rst = '1'` and `rst /= '0'` the
    // other one
    const bool activeLow = (levelp->charValue == '0') == (testp->text == "=");
    // The edge test is the sensitivity now, so the branches never see it; an edge call
    // that remains in a branch is that signal's level on the edge
    m_edgeAsLevel = true;
    m_edgeAsPrev = false;
    m_initial = false;
    m_varVals.clear();
    AstNode* const clockedp = lowerStmtList(ifStmt.branches[1].second);
    AstNode* const resetp = lowerStmtList(ifStmt.branches[0].second);
    AstNodeExpr* const condp = lowerCond(*testp);
    if (!clockedp || !resetp || !condp) {
        if (clockedp) clockedp->deleteTree();
        if (resetp) resetp->deleteTree();
        if (condp) condp->deleteTree();
        return true;  // The failure is reported; the caller must not lower the body again
    }
    AstSenItem* itemsp
        = new AstSenItem{process.flp, rising ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE,
                         new AstVarRef{process.flp, clockVarp, VAccess::READ}};
    itemsp = AstNode::addNext<AstSenItem, AstSenItem>(
        itemsp,
        new AstSenItem{process.flp, activeLow ? VEdgeType::ET_NEGEDGE : VEdgeType::ET_POSEDGE,
                       new AstVarRef{process.flp, resetVarp, VAccess::READ}});
    AstNode* const bodyp = new AstIf{process.flp, condp, resetp, clockedp};
    m_modp->addStmtsp(new AstAlways{process.flp, VAlwaysKwd::ALWAYS,
                                    new AstSenTree{process.flp, itemsp}, bodyp});
    // A VHDL process runs once at time zero, when the clock has not had an edge: only a
    // reset that is already active assigns anything
    m_initial = true;
    m_varVals.clear();
    AstNode* const initp = lowerStmtList(process.body);
    m_initial = false;
    if (initp) m_modp->addStmtsp(new AstInitialStatic{process.flp, initp});
    return true;
}

Lowering::ScopeSave Lowering::saveScope() const {
    ScopeSave saved;
    saved.scope = m_scope;
    saved.consts = m_consts;
    saved.preConsts = m_preConsts;
    saved.types = m_types;
    saved.ambiguousTypes = m_ambiguousTypes;
    saved.ambiguousLiterals = m_ambiguousLiterals;
    saved.constValues = m_constValues;
    saved.constTypes = m_constTypes;
    saved.enumConsts = m_enumConsts;
    saved.subprograms = m_subprograms;
    return saved;
}

void Lowering::restoreScope(const ScopeSave& saved) {
    m_scope = saved.scope;
    m_consts = saved.consts;
    m_preConsts = saved.preConsts;
    m_types = saved.types;
    m_ambiguousTypes = saved.ambiguousTypes;
    m_ambiguousLiterals = saved.ambiguousLiterals;
    m_constValues = saved.constValues;
    m_constTypes = saved.constTypes;
    m_enumConsts = saved.enumConsts;
    m_subprograms = saved.subprograms;
}

//! Declare the types, constants and variables of a process declarative part
void Lowering::emitProcessDecls(const Process& process, std::set<std::string>& varNames) {
    // A type or subtype in the same declarative part may name a constant declared there
    preScanIntConstants(process.decls);
    registerTypes(process.types, process.decls);
    registerSubprograms(process.subprograms, -1, true);
    // An alias names a designator, which may be a signal the loop below declares
    for (const AliasDecl& alias : process.aliases) {
        if (!alias.hasSignature) registerAlias(alias);
    }
    for (const Decl& decl : process.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectRealObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            // Mangle so the same variable name in two processes stays distinct
            const std::string mangled
                = "vhdlvar_" + std::to_string(++m_varSeq) + "__" + name.first;
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
    finishDeferredTypes();
}

//! Register a declared name; a local declaration hides a name from a used package
bool Lowering::reserveName(const std::string& folded, const std::string& spelling, FileLine* flp) {
    if (m_imported.erase(folded)) {  // The local declaration shadows the package member
        m_consts.erase(folded);
        m_constValues.erase(folded);
        m_constTypes.erase(folded);
        m_enumConsts.erase(folded);
        m_realConsts.erase(folded);
        m_types.erase(folded);
        return true;
    }
    if (m_scope.count(folded) || m_consts.count(folded) || m_constValues.count(folded)
        || m_enumConsts.count(folded) || m_realConsts.count(folded) || m_types.count(folded)) {
        flp->v3error("VHDL: duplicate declaration of '" + spelling + "'");
        return false;
    }
    return true;
}

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
    } else if (type.flp && resolveType(type, constType)) {
        intConstant = constType.isInt();
        realConstant = constType.kind == VType::REAL;
    }
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
            }
            return;
        }
        // A call whose body this pass cannot read: the body's pass completes the constant
        // rather than reporting the call (IEEE 1800-2023 6.6.1). A name with arguments
        // parses as an indexed name and becomes a call once the prefix names a subprogram.
        std::string callName;
        if (initp->kind == Expr::CALL) {
            callName = initp->text;
        } else if (initp->kind == Expr::INDEX && initp->a) {
            callName = initp->a->text;
        }
        if (deferredOk && !callName.empty()) {
            const auto refs = visibleSubprograms(callName);
            const bool anyBody
                = std::any_of(refs.begin(), refs.end(),
                              [](const SubprogramDecl* const subp) { return subp->hasBody; });
            if (!refs.empty() && !anyBody) {
                m_deferredConsts.push_back(DeferredConst{names, &type, initp, flp, m_unitName});
                return;
            }
        }
        // A call to a declared function is static as well: lower and fold it below
        if (!constType.dtp) constType = intType(flp);
    }
    if (!constType.dtp) {
        unsupportedError(flp, "constant of an unresolved type");
        valuep->deleteTree();
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
bool Lowering::registerGenerics(const std::vector<GenericDecl>& generics,
                                const GenericBindings* bindings, bool probe) {
    bool ok = true;
    bool unbound = false;  // A generic here has neither a binding nor a default
    const auto registerString
        = [&](FileLine* flp, const std::string& folded, const std::string& text) {
              if (text.empty()) {
                  unsupportedError(flp, "empty string generic");
                  return;
              }
              // A string generic's value is fixed at elaboration, so register it as a typed
              // constant of packed 8-bit character codes rather than a variable; that lets a
              // reference fold, e.g. `if val = "one" generate`.
              const VType type = stringType(flp, static_cast<int>(text.size()));
              AstConst* const valuep = makeStringConst(flp, text);
              m_ownedConsts.push_back(std::unique_ptr<AstConst>(valuep));
              m_constValues[folded] = m_ownedConsts.back().get();
              m_constTypes[folded] = type;
              m_stringConsts[folded] = text;
          };
    for (const GenericDecl& gen : generics) {
        for (const auto& name : gen.names) {
            if (unbound) continue;  // An earlier generic has no value, so nothing can elaborate
            if (gen.isType) {
                if (bindings) {
                    const auto it = bindings->find(name.first);
                    if (it != bindings->end() && it->second.isType) {
                        if (!probe) {
                            m_types[name.first] = it->second.typeValue;
                            m_imported.insert(name.first);
                        }
                        continue;
                    }
                }
                if (!probe) {
                    ok = false;  // A type generic has no elaboratable default
                    unbound = true;
                }
                continue;
            }
            if (gen.isSubprogram) {
                if (bindings) {
                    const auto it = bindings->find(name.first);
                    if (it != bindings->end() && it->second.subp) {
                        if (!probe) {
                            m_subprograms[name.first].push_back(
                                SubprogramRef{it->second.subp, false, -1});
                            m_imported.erase(name.first);
                        }
                        continue;
                    }
                }
                if (!probe) {
                    ok = false;  // A subprogram generic has no elaboratable default
                    unbound = true;
                }
                continue;
            }
            if (gen.type.mark == "string") {
                const Expr* actualp = nullptr;
                if (bindings) {
                    const auto it = bindings->find(name.first);
                    if (it != bindings->end() && it->second.isString) {
                        actualp = nullptr;
                        if (!probe) registerString(gen.flp, name.first, it->second.stringValue);
                        continue;
                    }
                }
                if (gen.init && gen.init->kind == Expr::STRING_LIT) { actualp = gen.init.get(); }
                if (actualp) {
                    if (!probe) registerString(gen.flp, name.first, actualp->spelling);
                } else if (!probe) {
                    ok = false;  // No default, so only a binding can elaborate the base
                    unbound = true;
                }
                continue;
            }
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
                unbound = true;
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

void Lowering::importPackage(const std::string& name, const std::string& only) {
    if (only.empty()) {
        if (name == "std_logic_signed") m_stdLogicSigned = true;
        if (name == "std_logic_arith") m_stdLogicArith = true;
    }
    if (!m_packagesp) return;
    const auto pkgIt = m_packagesp->packages.find(name);
    if (pkgIt == m_packagesp->packages.end()) return;  // Standard package: intrinsics
    const PackageInfo& info = pkgIt->second;
    // A single-declaration use makes only the declaration it names visible
    const auto wants = [&](const std::string& key) { return only.empty() || key == only; };
    for (const auto& alias : info.aliases) {
        if (!wants(alias.first)) continue;
        if (m_aliases.count(alias.first)) continue;
        m_aliases[alias.first] = alias.second;
    }

    const bool ownSpec = !m_unitName.empty() && name == m_unitName;
    for (const auto& type : info.types) {
        if (!wants(type.first)) continue;
        if (ownSpec) {
            // A package body sees its own specification closer than any imported package
            m_types[type.first] = type.second;
            m_ambiguousTypes.erase(type.first);
            continue;
        }
        const auto existing = m_types.find(type.first);
        if (existing != m_types.end()) {
            if (!existing->second.typeName.empty() && !type.second.typeName.empty()
                && existing->second.typeName != type.second.typeName) {
                m_ambiguousTypes.insert(type.first);
            }
            continue;
        }
        m_types[type.first] = type.second;
        m_imported.insert(type.first);
    }
    for (const auto& constant : info.consts) {
        if (!wants(constant.first)) continue;
        if (m_consts.count(constant.first) || m_constValues.count(constant.first)) continue;
        m_consts[constant.first] = constant.second;
        m_imported.insert(constant.first);
    }
    for (const auto& value : info.constValues) {
        if (!wants(value.first)) continue;
        if (m_consts.count(value.first) || m_constValues.count(value.first)) continue;
        m_ownedConsts.push_back(std::unique_ptr<AstConst>(value.second->cloneTree(false)));
        m_constValues[value.first] = m_ownedConsts.back().get();
        m_imported.insert(value.first);
    }
    for (const auto& type : info.constTypes) {
        if (!wants(type.first)) continue;
        if (m_constTypes.count(type.first)) continue;
        m_constTypes[type.first] = type.second;
    }
    for (const auto& realConst : info.realConsts) {
        if (!wants(realConst.first)) continue;
        if (m_realConsts.count(realConst.first)) continue;
        m_realConsts[realConst.first] = realConst.second;
        m_imported.insert(realConst.first);
    }
    if (only.empty()) {
        m_ambiguousLiterals.insert(info.ambiguousLiterals.begin(), info.ambiguousLiterals.end());
    }
    for (const auto& literal : info.enumConsts) {
        if (!wants(literal.first)) continue;
        const auto existing = m_enumConsts.find(literal.first);
        if (existing != m_enumConsts.end()) {
            if (existing->second->dtypep() != literal.second->dtypep()) {
                m_ambiguousLiterals.insert(literal.first);
            }
            continue;
        }
        m_ownedConsts.push_back(std::unique_ptr<AstConst>(literal.second->cloneTree(false)));
        m_enumConsts[literal.first] = m_ownedConsts.back().get();
        m_imported.insert(literal.first);
    }
    for (const SubprogramDecl* const subp : info.subprograms) {
        if (!wants(subp->name)) continue;
        std::vector<SubprogramRef>& refs = m_subprograms[subp->name];
        const bool known = std::any_of(refs.begin(), refs.end(),
                                       [=](const SubprogramRef& r) { return r.declp == subp; });
        if (known) continue;
        refs.push_back(SubprogramRef{subp, false, -1});
        m_imported.insert(subp->name);
    }
    // A synonym is resolved here, where the subprogram it designates is visible too
    for (const AliasDecl* const aliasp : info.subprogramAliases) {
        if (!wants(aliasp->name)) continue;
        registerSubprogramAlias(*aliasp);
    }
}

void Lowering::emitPackage(const PackageUnit& pkg, PackageTable::Impl& table,
                           const GenericBindings* bindings, const std::string& publishName) {
    const std::string& pubName = publishName.empty() ? pkg.name : publishName;
    m_unitName = pubName;

    // A package sees its own declarations and the packages its `use` clauses name
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_preConsts.clear();
    m_ownedConsts.clear();
    m_imported.clear();
    m_ambiguousTypes.clear();
    m_arrayDtypes.clear();
    m_stdLogicSigned = false;
    m_stdLogicArith = false;
    m_scope.clear();
    m_aliases.clear();
    m_subprograms.clear();
    // The packages named by the unit's `use` clauses are visible here as well, as they
    // are in an entity or an architecture. What the package declares itself wins: those
    // registrations overwrite, and `importPackage` never replaces a name it finds.
    for (const auto& use : pkg.uses) importPackage(use.pkg, use.decl);
    // A body is nested in its specification, so what the specification declares, and what
    // its own `use` clauses made visible to it, is visible in the body too. The
    // specification is always published first: lowerPackages runs a pass over the
    // specifications before any body, and an instance emits its specification directly
    // before its body.
    if (pkg.isBody) importPackage(pubName);
    // A generic package is emitted once per instance, with its generics already bound
    if (bindings) registerGenerics(pkg.generics, bindings, false);
    // Integer constants are folded first so a type range (a record element, an array
    // bound, an integer subtype) that names one resolves during type registration,
    // exactly as an entity or architecture prescans before registering its types.
    preScanIntConstants(pkg.decls);
    registerTypes(pkg.types, pkg.decls);
    // Subprograms are visible to the declarative items below, including deferred
    // constants, and to an alias renaming one of them
    registerSubprograms(pkg.subprograms, -1);
    for (const AliasDecl& alias : pkg.aliases) registerAlias(alias);
    // Preliminary snapshot: a constant's value may already call a subprogram, whose body
    // evaluates against this scope and not against whatever the caller has
    m_moduleScope = saveScope();
    for (const Decl& decl : pkg.decls)
        emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, !pkg.isBody);
    // A constant whose call had no body when the unit was first read is completed now
    // that the bodies are read. It runs after the unit's own constants, because the call
    // may name one of them, and only the unit that declared it completes it: the scope
    // of another unit is not its own
    std::vector<DeferredConst> owned;
    std::vector<DeferredConst> others;
    for (const DeferredConst& deferred : m_deferredConsts) {
        (deferred.owner == m_unitName ? owned : others).push_back(deferred);
    }
    m_deferredConsts = std::move(others);
    for (const DeferredConst& deferred : owned) {
        // A later pass may already have emitted it now that the bodies are read
        bool present = false;
        for (const auto& name : deferred.names) {
            if (m_consts.count(name.first) || m_constValues.count(name.first)) present = true;
        }
        if (present) continue;
        emitConstant(deferred.names, *deferred.typep, deferred.initp, deferred.flp, true);
    }
    // A type that names a constant this unit could not complete waits for the pass that
    // does complete it, rather than reporting the name as unknown
    const bool stillDeferred
        = std::any_of(m_deferredConsts.begin(), m_deferredConsts.end(),
                      [&](const DeferredConst& d) { return d.owner == m_unitName; });
    if (!stillDeferred) finishDeferredTypes();

    PackageInfo info;
    info.aliases = m_aliases;
    info.types = std::move(m_types);
    info.consts = std::move(m_consts);
    info.constValues = std::move(m_constValues);
    info.constTypes = std::move(m_constTypes);
    info.enumConsts = std::move(m_enumConsts);
    info.ambiguousLiterals = m_ambiguousLiterals;
    info.realConsts = std::move(m_realConsts);
    info.owned = std::move(m_ownedConsts);
    for (const SubprogramDecl& sub : pkg.subprograms) info.subprograms.push_back(&sub);
    for (const AliasDecl& alias : pkg.aliases) {
        if (alias.hasSignature) info.subprogramAliases.push_back(&alias);
    }
    m_types.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_realConsts.clear();
    m_preConsts.clear();
    m_ownedConsts.clear();
    m_aliases.clear();

    const auto existing = table.packages.find(pubName);
    if (existing == table.packages.end()) {
        table.packages.emplace(pubName, std::move(info));
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
    for (const auto& realConst : info.realConsts) {
        dest.realConsts[realConst.first] = realConst.second;
    }
    for (std::unique_ptr<AstConst>& constp : info.owned) dest.owned.push_back(std::move(constp));
    for (const SubprogramDecl* const subp : info.subprograms) dest.subprograms.push_back(subp);
    for (const AliasDecl* const aliasp : info.subprogramAliases) {
        dest.subprogramAliases.push_back(aliasp);
    }
}

bool Lowering::emit(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
                    const GenericBindings* bindings, const PortBoundsMap* portBounds) {
    m_modp = modp;

    m_subprograms.clear();  // Packages published these only through importPackage
    for (const auto& use : entity.uses) importPackage(use.pkg, use.decl);
    for (const auto& use : arch.uses) importPackage(use.pkg, use.decl);

    // A type's range may call a pure function, so make the subprograms visible
    // before registering any type
    registerSubprograms(entity.subprograms, -1);
    registerSubprograms(arch.subprograms, -1);
    // The scope an inlined body evaluates against, taken before the first item that may
    // call a subprogram -- a type's range or a generic default -- and taken again below
    // once the generics and the architecture's types are registered as well
    m_moduleScope = saveScope();

    // Types are visible to the whole declarative part. A type's range may depend on
    // an integer constant, which may in turn depend on a generic, so register the
    // pieces in dependency order.
    preScanIntConstants(entity.decls);
    registerTypes(entity.types, entity.decls);

    // Generics are constants visible to the port clause and the architecture
    if (!registerGenerics(entity.generics, bindings, false)) {
        m_modp = nullptr;
        m_scope.clear();
        m_consts.clear();
        m_constValues.clear();
        m_constTypes.clear();
        m_enumConsts.clear();
        m_ambiguousLiterals.clear();
        m_ownedConsts.clear();
        m_types.clear();
        m_deferredTypes.clear();
        m_preConsts.clear();
        return false;
    }

    // With generics bound, integer constants can size the architecture's types
    preScanIntConstants(entity.decls);
    preScanIntConstants(arch.decls);

    // Entity declarative items are visible to every architecture of the entity
    int entityVarSeq = 0;
    for (const Decl& decl : entity.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectRealObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            const std::string mangled
                = "vhdlext_" + std::to_string(++entityVarSeq) + "__" + name.first;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, mangled, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.kind == Decl::VARIABLE) m_variables.insert(name.first);
            if (decl.kind == Decl::SIGNAL && !isResolvedType(type)) {
                m_unresolvedSignals.insert(varp);
            }
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
    finishDeferredTypes();

    // The architecture's own types come after the entity's items, because a constraint may
    // name one of the entity's constants and the loop above emits them
    registerTypes(arch.types, arch.decls);
    // A subprogram alias has to exist before the declarations, whose initializers may call it
    for (const AliasDecl& alias : arch.aliases) {
        if (!alias.hasSignature) continue;
        registerAlias(alias);
    }
    // Preliminary snapshot: declarative items may already call subprograms
    m_moduleScope = saveScope();

    int pinNum = 0;
    for (const PortDecl& port : entity.ports) {
        VType type;
        const PortBounds* boundsp = nullptr;
        if (portBounds) {
            const auto it = portBounds->find(port.names.empty() ? "" : port.names.front().first);
            if (it != portBounds->end()) boundsp = &it->second;
        }
        if (boundsp && boundsp->isRecord) {
            // An unconstrained record port: each element's range comes from the actual
            if (!resolveType(port.type, type) || type.kind != VType::RECORD) continue;
            for (size_t i = 0; i < boundsp->memberNames.size(); ++i) {
                for (VField& field : type.fields) {
                    if (field.name != boundsp->memberNames[i]) continue;
                    setRecordField(entity.flp, field, boundsp->memberLeft[i],
                                   boundsp->memberRight[i], boundsp->memberAscending[i]);
                }
            }
            finalizeRecordFields(entity.flp, type);
        } else if (boundsp) {
            // An unconstrained port specialized from the actual at this instance
            VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
            VSigning signing = VSigning::NOSIGN;
            if (port.type.mark == "bit_vector") kwd = VBasicDTypeKwd::BIT;
            signing = vectorSigning(port.type.mark);
            type = vectorType(entity.flp, kwd, signing,
                              std::abs(boundsp->left - boundsp->right) + 1);
            type.left = boundsp->left;
            type.right = boundsp->right;
            type.ascending = boundsp->ascending;
            type.dtp = findVector(entity.flp, kwd, signing, type.left, type.right, type.ascending);
        } else if (!resolveType(port.type, type)) {
            continue;
        }
        if (rejectRealObject(type, port.flp, /*allowUnconstrained:*/ true)) continue;
        if (!type.dtp) {
            unsupportedError(port.flp, "port of type '" + port.type.mark
                                           + "' has no resolvable index constraint");
            continue;
        }
        for (const auto& name : port.names) {
            if (!m_scope.emplace(name.first, VarInfo{}).second) {
                port.flp->v3error("VHDL: duplicate declaration of '" + name.second + "'");
                continue;
            }
            // An inout port is a net: V3Tristate connects its drivers and converts it to
            // the input plus output-enable/output pins of the two-state model.
            const VVarType varType = port.dir == PortMode::INOUT ? VVarType::WIRE : VVarType::PORT;
            AstVar* const varp = new AstVar{port.flp, varType, name.first, type.dtp};
            const VDirection direction = port.dir == PortMode::IN    ? VDirection::INPUT
                                         : port.dir == PortMode::OUT ? VDirection::OUTPUT
                                                                     : VDirection::INOUT;
            varp->direction(direction);
            // V3Tristate distinguishes a converted tristate input from an input that was
            // declared as one by comparing against the declared direction.
            varp->declDirection(direction);
            varp->pinNum(++pinNum);
            // VHDL ports are signals, so procedural assignment is legal
            varp->declTyped(true);
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (!isResolvedType(type)) m_unresolvedSignals.insert(varp);
        }
    }

    for (const Decl& decl : arch.decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectRealObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            AstVar* const varp = new AstVar{decl.flp, VVarType::VAR, name.first, type.dtp};
            m_modp->addStmtsp(varp);
            m_scope[name.first] = VarInfo{varp, type};
            if (decl.kind == Decl::VARIABLE) m_variables.insert(name.first);
            if (decl.kind == Decl::SIGNAL && !isResolvedType(type)) {
                m_unresolvedSignals.insert(varp);
            }
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
    finishDeferredTypes();
    // An object alias comes last: its mark may read a signal the loop above declares
    for (const AliasDecl& alias : arch.aliases) {
        if (alias.hasSignature) continue;
        registerAlias(alias);
    }

    // Subprograms of the declarative part capture the module scope as it stands here
    m_moduleScope = saveScope();
    // The entity's own statement part drives the module as well
    for (const auto& stmtp : entity.stmts) emitConcurrent(*stmtp);
    for (const auto& stmtp : arch.stmts) emitConcurrent(*stmtp);
    checkDrivers();

    m_modp = nullptr;
    m_scope.clear();
    m_consts.clear();
    m_constValues.clear();
    m_constTypes.clear();
    m_enumConsts.clear();
    m_ambiguousLiterals.clear();
    m_ownedConsts.clear();
    m_types.clear();
    m_preConsts.clear();
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

//! Bind an instance's generics and its unconstrained port ranges, and return the module
//! specialized for that binding; the base module when there is nothing to bind
AstModule* Lowering::specializeInstance(const Concurrent& conc, const EntityUnit& entity,
                                        AstModule* basep, const PortBoundsMap& portBounds) {
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
        if (gen.isType) {
            GenericBinding binding;
            binding.isType = true;
            if (!resolveTypeActual(*assocp->actual, binding.typeValue)) {
                unsupportedError(assocp->flp, "generic type actual");
                freeBindings(bindings);
                return nullptr;
            }
            bindings[items[k].folded] = binding;
            continue;
        }
        if (gen.isSubprogram) {
            // The actual names a subprogram visible where the instance is declared
            GenericBinding binding;
            const std::vector<const SubprogramDecl*> visible
                = visibleSubprograms(assocp->actual->text);
            if (visible.empty()) {
                unsupportedError(assocp->flp, "generic subprogram parameter actual");
                freeBindings(bindings);
                return nullptr;
            }
            binding.subp = visible.back();
            bindings[items[k].folded] = binding;
            continue;
        }
        if (gen.type.mark == "string") {
            const Expr* const actualp = assocp ? assocp->actual.get() : gen.init.get();
            if (!actualp) {
                assocp->flp->v3error("VHDL: string generic '" + items[k].spelling
                                     + "' has no default value and no association");
                freeBindings(bindings);
                return nullptr;
            }
            std::string stringValue;
            if (actualp->kind == Expr::STRING_LIT) {
                stringValue = actualp->spelling;
            } else if (actualp->kind == Expr::NAME) {
                // A string generic passed on from the enclosing unit, so its text
                // comes from that unit's generic
                const auto outer = m_stringConsts.find(actualp->text);
                if (outer != m_stringConsts.end()) stringValue = outer->second;
            }
            if (stringValue.empty()) {
                unsupportedError(actualp->flp, "string generic actual");
                freeBindings(bindings);
                return nullptr;
            }
            GenericBinding binding;
            binding.isString = true;
            binding.stringValue = stringValue;
            bindings[items[k].folded] = binding;
            continue;
        }
        // The declared type resolves in the entity's scope, where the generics that
        // precede this one already carry the value bound at this instance
        ScopeSave saved = saveScope();
        const std::set<std::string> savedImported = m_imported;
        for (const auto& use : entity.uses) importPackage(use.pkg, use.decl);
        for (const TypeDecl& typeDecl : entity.types) registerType(typeDecl);
        registerGenerics(entity.generics, &bindings, true);
        // The declared type may itself call a function, whose body evaluates against the
        // module scope, so take it with this instance's bindings and the entity's packages
        m_moduleScope = saveScope();
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
            AstConst* const foldedp = valuep ? foldStatic(valuep) : nullptr;
            if (foldedp) {
                valuep->deleteTree();
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
    if (bindings.empty() && portBounds.empty()) return basep;  // Nothing to bind
    std::string key = entity.name;
    // The port-bounds part leads the key: V3VhdlNative.cpp looks for the prefix
    // "<entity>|ports" to tell that an unconstrained-port entity was specialized
    if (!portBounds.empty()) {
        key += "|ports";
        for (const auto& entry : portBounds) {
            const PortBounds& bounds = entry.second;
            key += "|" + entry.first + "=";
            if (bounds.isRecord) {
                for (size_t i = 0; i < bounds.memberNames.size(); ++i) {
                    key += bounds.memberNames[i] + "(" + std::to_string(bounds.memberLeft[i]) + ":"
                           + std::to_string(bounds.memberRight[i])
                           + (bounds.memberAscending[i] ? "to" : "downto") + ")";
                }
            } else {
                key += std::to_string(bounds.left) + ":" + std::to_string(bounds.right) + ":"
                       + (bounds.ascending ? "to" : "downto");
            }
        }
    }
    for (const auto& entry : bindings) {
        key += "|" + entry.first + "=";
        if (entry.second.isType) {
            const VType& type = entry.second.typeValue;
            key += std::to_string(static_cast<int>(type.kind)) + ":" + std::to_string(type.width)
                   + ":" + std::to_string(type.left) + ":" + std::to_string(type.right) + ":"
                   + (type.ascending ? "to" : "downto") + ":"
                   + std::to_string(static_cast<int>(type.signing));
        } else if (entry.second.isInt) {
            key += std::to_string(entry.second.intValue);
        } else if (entry.second.isString) {
            key += entry.second.stringValue;
        } else if (entry.second.subp) {
            // Unique per actual subprogram declaration within this run
            key += "subp:" + std::to_string(reinterpret_cast<uintptr_t>(entry.second.subp));
        } else {
            key += entry.second.valuep->num().ascii(false);
        }
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
    AstModule* const specp
        = new AstModule{entity.flp, entity.name + "__Vspec" + std::to_string(++m_designp->specSeq),
                        entity.libname};
    specp->inLibrary(entity.inLibrary);
    m_rootp->addModulesp(specp);
    Lowering child{m_rootp, m_packagesp, m_modulesp, m_designp};
    const bool ok
        = child.emit(entity, *archIt->second, specp, bindings.empty() ? nullptr : &bindings,
                     portBounds.empty() ? nullptr : &portBounds);
    --m_designp->specDepth;
    freeBindings(bindings);
    cache[key] = ok ? specp : nullptr;
    return ok ? specp : nullptr;
}

bool Lowering::resolveTypeActual(const Expr& actual, VType& out) {
    TypeSpec spec;
    spec.flp = actual.flp;
    if (actual.kind == Expr::NAME) {
        spec.mark = actual.text;
        return resolveType(spec, out);
    }
    if ((actual.kind == Expr::SLICE || actual.kind == Expr::INDEX) && actual.a
        && actual.a->kind == Expr::NAME) {
        spec.mark = actual.a->text;
        if (actual.kind != Expr::SLICE || !actual.b || !actual.c) return false;
        spec.hasRange = true;
        spec.ascending = actual.text == "to";
        spec.left = cloneExpr(*actual.b);
        spec.right = cloneExpr(*actual.c);
        return resolveType(spec, out);
    }
    return false;
}

bool Lowering::derivePortBounds(const EntityUnit& entity, const Concurrent& conc,
                                const PortDecl& port, PortBounds& out) {
    const std::string& portName = port.names.front().first;
    int targetIndex = -1;
    int index = 0;
    for (const PortDecl& candidate : entity.ports) {
        for (const auto& name : candidate.names) {
            if (name.first == portName) targetIndex = index;
            ++index;
        }
    }
    const bool positional
        = std::any_of(conc.associations.begin(), conc.associations.end(),
                      [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    std::vector<const PortAssoc*> assocs;
    for (size_t i = 0; i < conc.associations.size(); ++i) {
        const PortAssoc& assoc = conc.associations[i];
        if (positional ? static_cast<int>(i) == targetIndex
                       : assoc.formal == port.names.front().first) {
            assocs.push_back(&assoc);
        }
    }
    bool have = false;
    bool ascending = false;
    int left = 0;
    int right = 0;
    bool ranges = false;
    bool wholes = false;
    const auto addBounds = [&](int64_t l, int64_t r, bool asc) -> bool {
        if (asc != ascending && have) return false;
        ascending = asc;
        const int lo = static_cast<int>(std::min(l, r));
        const int hi = static_cast<int>(std::max(l, r));
        if (!have) {
            left = asc ? lo : hi;
            right = asc ? hi : lo;
            have = true;
            return true;
        }
        left = asc ? std::min(left, lo) : std::max(left, hi);
        right = asc ? std::max(right, hi) : std::min(right, lo);
        return true;
    };
    for (const PortAssoc* const assocp : assocs) {
        if (assocp->hasFormalRange) {
            int64_t l = 0;
            int64_t r = 0;
            if (!staticInt(assocp->formalLeft, l)
                || (assocp->formalRight && !staticInt(assocp->formalRight, r))) {
                unsupportedError(assocp->flp, "non-static port association range");
                return false;
            }
            if (!assocp->formalRight) r = l;
            if (!addBounds(l, r, assocp->formalAscending)) {
                unsupportedError(assocp->flp, "mixed directions on an unconstrained port");
                return false;
            }
            ranges = true;
            continue;
        }
        if (!assocp->actual) {
            if (!port.init) {
                unsupportedError(assocp->flp, "open unconstrained port with no default value");
                return false;
            }
            AstNodeExpr* const valuep = lowerExpr(*port.init, nullptr);
            if (!valuep) return false;
            const VType type = typeOfDType(valuep->dtypep());
            valuep->deleteTree();
            if (!type.isVector() || !addBounds(type.left, type.right, type.ascending)) {
                unsupportedError(assocp->flp, "default value of an unconstrained port");
                return false;
            }
            wholes = true;
            continue;
        }
        VType type;
        if (!typeOfDesignator(*assocp->actual, type)) {
            AstNodeExpr* const valuep = lowerExpr(*assocp->actual, nullptr);
            if (!valuep) return false;
            type = typeOfDType(valuep->dtypep());
            valuep->deleteTree();
        }
        if (!type.isVector() || !addBounds(type.left, type.right, type.ascending)) {
            unsupportedError(assocp->flp, "actual associated with an unconstrained port");
            return false;
        }
        wholes = true;
    }
    if (!have && port.init) {
        // An omitted input port takes its declared default, whose type fixes the range
        AstNodeExpr* const valuep = lowerExpr(*port.init, nullptr);
        if (!valuep) return false;
        const VType type = typeOfDType(valuep->dtypep());
        valuep->deleteTree();
        if (!type.isVector() || !addBounds(type.left, type.right, type.ascending)) {
            unsupportedError(port.flp, "default value of an unconstrained port");
            return false;
        }
    }
    if (!have) {
        unsupportedError(port.flp, "unconstrained port '" + port.names.front().second
                                       + "' with no association");
        return false;
    }
    if (ranges && wholes) {
        unsupportedError(port.flp, "mixed whole and part associations of an unconstrained port");
        return false;
    }
    out.left = left;
    out.right = right;
    out.ascending = ascending;
    return true;
}

bool Lowering::resolvePortRecordType(const EntityUnit& entity, const PortDecl& port,
                                     VType& out) const {
    if (port.type.hasRange || !port.type.constraintMark.empty()) return false;
    const auto typeIt = m_types.find(port.type.mark);
    if (typeIt != m_types.end() && typeIt->second.kind == VType::RECORD) {
        out = typeIt->second;
        return true;
    }
    // The parent can name a record's elements without importing the package that declares
    // the record, so fall back to the child's own context.
    if (m_packagesp) {
        for (const auto& use : entity.uses) {
            const auto pkgIt = m_packagesp->packages.find(use.pkg);
            if (pkgIt == m_packagesp->packages.end()) continue;
            const auto fieldIt = pkgIt->second.types.find(port.type.mark);
            if (fieldIt != pkgIt->second.types.end() && fieldIt->second.kind == VType::RECORD) {
                out = fieldIt->second;
                return true;
            }
        }
    }
    return false;
}

bool Lowering::deriveRecordBounds(const EntityUnit& entity, const Concurrent& conc,
                                  const PortDecl& port, PortBounds& out) {
    const std::string& portName = port.names.front().first;
    int targetIndex = -1;
    int index = 0;
    for (const PortDecl& candidate : entity.ports) {
        for (const auto& name : candidate.names) {
            if (name.first == portName) targetIndex = index;
            ++index;
        }
    }
    const bool positional
        = std::any_of(conc.associations.begin(), conc.associations.end(),
                      [](const PortAssoc& assoc) { return assoc.formal.empty(); });
    std::vector<const PortAssoc*> assocs;
    for (size_t i = 0; i < conc.associations.size(); ++i) {
        const PortAssoc& assoc = conc.associations[i];
        if (positional ? static_cast<int>(i) == targetIndex
                       : assoc.formal == port.names.front().first) {
            assocs.push_back(&assoc);
        }
    }
    const bool anyWhole = std::any_of(assocs.begin(), assocs.end(), [](const PortAssoc* assocp) {
        return assocp->formalMember.empty();
    });
    const bool anyMember = std::any_of(assocs.begin(), assocs.end(), [](const PortAssoc* assocp) {
        return !assocp->formalMember.empty();
    });
    if (anyWhole && anyMember) {
        unsupportedError(conc.flp, "mixing whole-port and record-member associations");
        return false;
    }
    if (anyWhole && assocs.size() > 1) {
        conc.flp->v3error("VHDL: port '" + port.names.front().second
                          + "' is associated more than once");
        return false;
    }
    // The port's declared record fixes the element order and its constrained elements.
    // Each unconstrained element takes its range from the association that names it.
    VType baseType;
    if (!resolvePortRecordType(entity, port, baseType)) {
        unsupportedError(port.flp, "unconstrained record port");
        return false;
    }
    const size_t fieldCount = baseType.fields.size();
    std::vector<int> lefts(fieldCount);
    std::vector<int> rights(fieldCount);
    std::vector<bool> ascendings(fieldCount);
    std::vector<bool> have(fieldCount, false);
    std::vector<bool> seen(fieldCount, false);
    const auto fieldIndex = [&](const std::string& name) {
        for (size_t i = 0; i < fieldCount; ++i) {
            if (baseType.fields[i].name == name) return static_cast<int>(i);
        }
        return -1;
    };
    const auto actualTypeOf = [&](const Expr& actual, VType& type) {
        if (typeOfDesignator(actual, type)) return true;
        AstNodeExpr* const valuep = lowerExpr(actual, nullptr);
        if (!valuep) return false;
        type = typeOfDType(valuep->dtypep());
        valuep->deleteTree();
        return true;
    };
    const auto setMember = [&](const int field, const VType& type) {
        lefts[field] = type.left;
        rights[field] = type.right;
        ascendings[field] = type.ascending;
        have[field] = true;
    };
    for (const PortAssoc* const assocp : assocs) {
        if (assocp->hasFormalRange) {
            unsupportedError(assocp->flp, "part of a port that is not a bit vector");
            return false;
        }
        if (!assocp->formalMember.empty()) {
            const int field = fieldIndex(assocp->formalMember);
            if (field < 0) {
                assocp->flp->v3error("VHDL: record port '" + port.names.front().second
                                     + "' has no element '" + assocp->formalMemberSpelling + "'");
                return false;
            }
            if (seen[field]) {
                assocp->flp->v3error("VHDL: record port '" + port.names.front().second
                                     + "' element '" + assocp->formalMemberSpelling
                                     + "' is associated more than once");
                return false;
            }
            seen[field] = true;
            if (baseType.fields[field].width != 0) continue;  // range from the declaration
            if (!assocp->actual) {
                unsupportedError(assocp->flp, "open record-member port association");
                return false;
            }
            VType actualType;
            if (!actualTypeOf(*assocp->actual, actualType)) return false;
            if (!actualType.isVector()) {
                unsupportedError(assocp->flp, "record port element actual is not a vector");
                return false;
            }
            setMember(field, actualType);
            continue;
        }
        // A whole record actual fixes every unconstrained element's range
        if (!assocp->actual) {
            unsupportedError(assocp->flp, "open unconstrained record port with no default value");
            return false;
        }
        VType actualType;
        if (!actualTypeOf(*assocp->actual, actualType)) return false;
        if (actualType.kind != VType::RECORD) {
            unsupportedError(assocp->flp,
                             "actual of an unconstrained record port is not a record");
            return false;
        }
        for (size_t field = 0; field < fieldCount; ++field) {
            seen[field] = true;
            if (baseType.fields[field].width != 0) continue;
            const VField* actualp = nullptr;
            for (const VField& f : actualType.fields) {
                if (f.name == baseType.fields[field].name) actualp = &f;
            }
            if (!actualp || !actualp->type || !actualp->type->isVector()) {
                unsupportedError(assocp->flp,
                                 "unconstrained record port element has no vector actual");
                return false;
            }
            setMember(static_cast<int>(field), *actualp->type);
        }
    }
    out.isRecord = true;
    for (size_t field = 0; field < fieldCount; ++field) {
        if (baseType.fields[field].width != 0) continue;
        if (!have[field]) {
            unsupportedError(port.flp, "unconstrained record port element '"
                                           + baseType.fields[field].name + "' has no association");
            return false;
        }
        out.memberNames.push_back(baseType.fields[field].name);
        out.memberLeft.push_back(lefts[field]);
        out.memberRight.push_back(rights[field]);
        out.memberAscending.push_back(ascendings[field]);
    }
    return true;
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
    if (entityp) m_designp->instantiated.insert(conc.instanceName);
    PortBoundsMap portBounds;
    if (entityp) {
        for (const PortDecl& port : entityp->ports) {
            const bool unconstrainedRange
                = !port.type.hasRange && port.type.constraintMark.empty();
            const bool isRecordPort
                = unconstrainedRange && !unconstrainedPort(port)
                  && m_designp->unconstrainedRecordMembers.count(port.type.mark);
            if (!unconstrainedPort(port) && !isRecordPort) continue;
            PortBounds bounds;
            const bool ok = isRecordPort ? deriveRecordBounds(*entityp, conc, port, bounds)
                                         : derivePortBounds(*entityp, conc, port, bounds);
            if (!ok) return nullptr;
            for (const auto& name : port.names) portBounds[name.first] = bounds;
        }
        if (!entityp->generics.empty() || !portBounds.empty()) {
            modp = specializeInstance(conc, *entityp, modp, portBounds);
            if (!modp) return nullptr;
        }
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
    //! The record type a port was declared with, if any, for member associations
    auto recordPortType = [&](const AstVar* const portp) -> VType {
        VType none;
        if (!entityp) return none;
        for (const PortDecl& port : entityp->ports) {
            const bool match = std::any_of(port.names.begin(), port.names.end(),
                                           [&](const std::pair<std::string, std::string>& name) {
                                               return name.first == portp->name();
                                           });
            if (!match) continue;
            VType found;
            return resolvePortRecordType(*entityp, port, found) ? found : VType{};
        }
        return none;
    };
    auto recordField = [](const VType& rec, const std::string& member) -> const VField* {
        for (const VField& field : rec.fields)
            if (field.name == member) return &field;
        return nullptr;
    };
    // An unassociated or `open` input takes its declared default; anything else
    // stays unconnected (IEEE 1076-2008 6.5.7.1)
    auto declaredDefault = [&](const AstVar* const portp, const VType& aggType,
                               const VType& portType) -> AstNodeExpr* {
        if (!entityp || !portp->direction().isInput()) return nullptr;
        for (const PortDecl& port : entityp->ports) {
            if (!port.init) continue;
            const bool matches = std::any_of(port.names.begin(), port.names.end(),
                                             [&](const std::pair<std::string, std::string>& name) {
                                                 return name.first == portp->name();
                                             });
            if (!matches) continue;
            AstNodeExpr* defaultp = lowerExpr(*port.init, &aggType);
            if (defaultp) defaultp = resizeExpr(defaultp, portType, flp);
            return defaultp;
        }
        return nullptr;
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
                if (assoc.formal == portp->name()) assocs.push_back(&assoc);
            }
            // A whole port may be associated once; only a part of it (a slice or a record
            // element) may be associated alongside another part
            if (assocs.size() > 1
                && std::any_of(assocs.begin(), assocs.end(), [](const PortAssoc* assocp) {
                       return !assocp->hasFormalRange && assocp->formalMember.empty();
                   })) {
                flp->v3error("VHDL: port '" + assocs.front()->formalSpelling
                             + "' is associated more than once");
                return nullptr;
            }
        }
        const VType portType = typeOfDType(portp->dtypep());
        // The packed vector of a record port does not name its elements, so an aggregate
        // actual or default takes the record type the port is declared with. An
        // unconstrained record port takes the element ranges this instance fixed.
        VType recPortType = recordPortType(portp);
        const auto boundsIt = portBounds.find(portp->name());
        if (boundsIt != portBounds.end() && boundsIt->second.isRecord
            && recPortType.kind == VType::RECORD) {
            const PortBounds& bounds = boundsIt->second;
            for (size_t i = 0; i < bounds.memberNames.size(); ++i) {
                for (VField& field : recPortType.fields) {
                    if (field.name != bounds.memberNames[i]) continue;
                    setRecordField(flp, field, bounds.memberLeft[i], bounds.memberRight[i],
                                   bounds.memberAscending[i]);
                }
            }
            finalizeRecordFields(flp, recPortType);
        }
        const VType& aggType = recPortType.kind == VType::RECORD ? recPortType : portType;
        if (assocs.empty()) {
            AstPin* const pinp = new AstPin{flp, portp->pinNum(), portp->name(),
                                            declaredDefault(portp, aggType, portType)};
            pinp->modVarp(portp);
            pinsp = pinsp ? AstNode::addNext<AstPin, AstPin>(pinsp, pinp) : pinp;
            continue;
        }
        const bool partial
            = std::any_of(assocs.begin(), assocs.end(),
                          [](const PortAssoc* assocp) { return assocp->hasFormalRange; });
        const bool recordMember
            = std::any_of(assocs.begin(), assocs.end(),
                          [](const PortAssoc* assocp) { return !assocp->formalMember.empty(); });
        AstNodeExpr* actualp = nullptr;
        if (recordMember) {
            const VType& recType = recPortType;
            if (recType.kind != VType::RECORD) {
                unsupportedError(flp, "record-member association on a non-record port");
                return nullptr;
            }
            for (const PortAssoc* const assocp : assocs) {
                if (assocp->formalMember.empty()) {
                    unsupportedError(assocp->flp,
                                     "mixing whole-port and record-member associations");
                    return nullptr;
                }
            }
            if (portp->direction().isOutput()) {
                // Each member actual is driven from its element of a packed temp the instance
                // writes, mirroring how the instance reads and writes record elements
                AstVar* const tempVarp
                    = new AstVar{flp, VVarType::VAR,
                                 "vhdlind_" + std::to_string(++m_loopSeq) + "__" + portp->name(),
                                 portp->dtypep()};
                m_modp->addStmtsp(tempVarp);
                AstNode* headp = nullptr;
                for (const PortAssoc* const assocp : assocs) {
                    if (!assocp->actual) continue;
                    const VField* const fieldp = recordField(recType, assocp->formalMember);
                    if (!fieldp) {
                        assocp->flp->v3error("VHDL: record port '" + portp->name()
                                             + "' has no element '" + assocp->formalMemberSpelling
                                             + "'");
                        if (headp) headp->deleteTree();
                        return nullptr;
                    }
                    AstNodeExpr* const lhsp = lowerDesignator(*assocp->actual, true);
                    if (!lhsp) {
                        if (headp) headp->deleteTree();
                        return nullptr;
                    }
                    AstSel* const rhsp
                        = new AstSel{flp, new AstVarRef{flp, tempVarp, VAccess::READ},
                                     fieldp->offset, fieldp->width};
                    rhsp->dtypep(fieldp->type->dtp);
                    AstNode* const assp = new AstAssign{flp, lhsp, rhsp};
                    headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, assp) : assp;
                }
                if (headp) {
                    m_modp->addStmtsp(new AstAlways{flp, VAlwaysKwd::CONT_ASSIGN, nullptr, headp});
                }
                actualp = new AstVarRef{flp, tempVarp, VAccess::WRITE};
            } else {
                // An input record packs its member actuals exactly like a record aggregate
                Expr agg{Expr::AGG_NAMED};
                agg.flp = flp;
                for (const PortAssoc* const assocp : assocs) {
                    if (!assocp->actual) {
                        unsupportedError(assocp->flp, "open record-member port association");
                        return nullptr;
                    }
                    ExprP choicep = std::make_unique<Expr>(Expr::NAME);
                    choicep->flp = flp;
                    choicep->text = assocp->formalMember;
                    choicep->spelling = assocp->formalMemberSpelling;
                    agg.choices.emplace_back();
                    agg.choices.back().push_back(std::move(choicep));
                    agg.args.push_back(cloneExpr(*assocp->actual));
                }
                actualp = lowerAggregate(agg, recType);
                if (!actualp) return nullptr;
            }
        } else if (!partial) {
            // The whole port connects to one actual; an `open` association leaves
            // it unconnected, so an input takes its declared default
            const PortAssoc& assoc = *assocs.front();
            if (assoc.actual) {
                const bool driven = portp->direction().isOutput() || portp->direction().isInout();
                AstNodeExpr* valuep = driven ? lowerDesignator(*assoc.actual, true)
                                             : lowerExpr(*assoc.actual, &aggType);
                if (!valuep) return nullptr;
                // IEEE 1076-2008 6.5.2.2: the actual must conform to the formal's type,
                // so a width or element-kind mismatch is reported rather than resized
                const VType actualType = typeOfLowered(*assoc.actual, valuep);
                bool conforms = true;
                if (actualType.isVector() && portType.isVector()) {
                    conforms = actualType.width == portType.width;
                    if (conforms && assoc.actual->kind == Expr::NAME) {
                        conforms = actualType.kwd == portType.kwd;
                    }
                }
                if (!conforms) {
                    assoc.flp->v3error("VHDL: actual of port '" + portp->name()
                                       + "' does not conform to the port's type "
                                         "(IEEE 1076-2008 6.5.2.2)");
                    valuep->deleteTree();
                    return nullptr;
                }
                actualp = resizeExpr(valuep, portType, flp);
            } else {
                actualp = declaredDefault(portp, aggType, portType);
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
                            = vectorType(flp, portType.kwd, VSigning::NOSIGN, connection.width);
                        valuep = lowerExpr(*connection.assocp->actual, &elementType);
                        if (!valuep) return nullptr;
                        valuep = resizeExpr(valuep, elementType, flp);
                    } else {
                        // An open input element contributes unknown bits
                        valuep = makeRepeatedConst(
                            flp, vectorType(flp, portType.kwd, VSigning::NOSIGN, connection.width),
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
                        = vectorType(flp, portType.kwd, VSigning::NOSIGN, connection.width);
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
                return portp->name() == assoc.formal;
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
    // A type or subtype in the same declarative part may name a constant declared there
    preScanIntConstants(decls);
    registerTypes(types, decls);
    for (const Decl& decl : decls) {
        if (decl.kind == Decl::CONSTANT) {
            emitConstant(decl.names, decl.type, decl.init.get(), decl.flp, false);
            continue;
        }
        VType type;
        if (!resolveType(decl.type, type)) continue;
        if (rejectRealObject(type, decl.flp)) continue;
        for (const auto& name : decl.names) {
            if (!reserveName(name.first, name.second, decl.flp)) continue;
            const std::string mangled
                = std::string{prefix} + std::to_string(++m_loopSeq) + "__" + name.first;
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
    finishDeferredTypes();
}

void Lowering::emitConcurrent(const Concurrent& conc) {
    const Concurrent& stmt = conc;
    if (stmt.kind == Concurrent::ASSERT) {
        // A concurrent assertion reports on every evaluation, and failure ends the run
        AstNode* const assertp
            = assertBody(stmt.flp, *stmt.target, stmt.value.get(), stmt.severity);
        if (assertp)
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, assertp});
        return;
    }
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
        bool up = stmt.rangeAscending;
        // `for i in obj'range generate` iterates the object's own bounds, which come from
        // its type just as a `prefix'range` slice does
        const Expr* const rangep
            = !stmt.hi && stmt.lo && stmt.lo->kind == Expr::ATTR
                      && (stmt.lo->text == "range" || stmt.lo->text == "reverse_range")
                  ? stmt.lo.get()
                  : nullptr;
        if (rangep) {
            if (!rangep->a || rangep->a->kind != Expr::NAME) {
                unsupportedError(stmt.flp, "generate over 'range of a non-name prefix");
                return;
            }
            const VType* const rangeTypep = typeOfName(*rangep->a);
            if (!rangeTypep) {
                unsupportedError(stmt.flp, "generate over 'range of an unknown name");
                return;
            }
            const bool reverse = rangep->text == "reverse_range";
            left = reverse ? rangeTypep->right : rangeTypep->left;
            right = reverse ? rangeTypep->left : rangeTypep->right;
            up = reverse ? !rangeTypep->ascending : rangeTypep->ascending;
        } else if (!stmt.hi && stmt.lo && stmt.lo->kind == Expr::NAME) {
            // `for i in <subtype> generate` iterates the subtype's own bounds
            // (IEEE 1076-2008 11.8), as a loop over a discrete subtype does
            const auto subtypeIt = m_types.find(stmt.lo->text);
            if (subtypeIt == m_types.end() || subtypeIt->second.kind != VType::INT) {
                unsupportedError(stmt.flp, "generate over a name that is not a subtype");
                return;
            }
            left = subtypeIt->second.intLow;
            right = subtypeIt->second.intHigh;
            up = left <= right;
        } else if (!staticInt(stmt.lo, left) || !staticInt(stmt.hi, right)) {
            unsupportedError(stmt.flp, "generate range that is not static");
            return;
        }
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
            AstConst* const foldedp = condp ? foldStatic(condp) : nullptr;
            if (condp) condp->deleteTree();
            if (!foldedp) {
                unsupportedError(stmt.flp, "generate condition that is not static");
                return;
            }
            const bool taken = foldedp->num().isTrue();
            foldedp->deleteTree();
            if (taken) {
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
    if (stmt.target->kind == Expr::AGG_POSITIONAL || stmt.target->kind == Expr::AGG_NAMED) {
        if (stmt.kind != Concurrent::ASSIGN) {
            unsupportedError(stmt.flp, "aggregate target in a conditional or selected assignment");
            return;
        }
        AstNode* const bodyp
            = lowerAggregateAssign(stmt.flp, *stmt.target, *stmt.value, false, true);
        if (bodyp) {
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr, bodyp});
        }
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
    const bool holds = !stmt.alternatives.empty()
                       && (stmt.alternatives.back().second
                           || std::any_of(stmt.alternatives.begin(), stmt.alternatives.end(),
                                          [](const std::pair<ExprP, ExprP>& alt) {
                                              return alt.first->kind == Expr::UNAFFECTED;
                                          }));
    if (stmt.kind == Concurrent::COND_ASSIGN && holds) {
        // A missing else or an unaffected branch makes the target hold its value
        AstNode* const bodyp = lowerLatchedAlternatives(stmt, ltype, lhsp);
        lhsp->deleteTree();
        if (bodyp) {
            m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS_LATCH, nullptr, bodyp});
        }
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
    if (!stmt.waveform.empty()) {
        // A waveform list repeats: each element after the first waits its own delay, and
        // the last element's delay closes the period (IEEE 1800-2023 10.6)
        AstNode* bodyp = new AstAssign{stmt.flp, lhsp, rhsp};
        const Expr* lastDelayp = stmt.delay.get();
        for (const auto& element : stmt.waveform) {
            if (element.second) {
                AstNode* const delayp = lowerDelay(*element.second, stmt.flp);
                if (!delayp) {
                    bodyp->deleteTree();
                    return;
                }
                bodyp = AstNode::addNext<AstNode, AstNode>(bodyp, delayp);
                lastDelayp = element.second.get();
            }
            AstNodeExpr* const elementp = lowerExpr(*element.first, &ltype);
            if (!elementp) {
                bodyp->deleteTree();
                return;
            }
            bodyp = AstNode::addNext<AstNode, AstNode>(
                bodyp, new AstAssign{stmt.flp, lhsp->cloneTree(false),
                                     resizeExpr(elementp, ltype, stmt.flp)});
        }
        if (lastDelayp) {
            AstNode* const delayp = lowerDelay(*lastDelayp, stmt.flp);
            if (!delayp) {
                bodyp->deleteTree();
                return;
            }
            bodyp = AstNode::addNext<AstNode, AstNode>(bodyp, delayp);
        }
        m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS, nullptr, bodyp});
        return;
    }
    if (stmt.delay && !staticZeroDelay(stmt.delay)) {
        // A concurrent delayed assignment repeats: it waits, then drives the target
        AstNode* const assignp = new AstAssign{stmt.flp, lhsp, rhsp};
        AstNode* const delayp = lowerDelay(*stmt.delay, stmt.flp);
        if (!delayp) {
            assignp->deleteTree();
            return;
        }
        AstNode* const bodyp = AstNode::addNext<AstNode, AstNode>(delayp, assignp);
        m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::ALWAYS, nullptr, bodyp});
        return;
    }
    m_modp->addStmtsp(new AstAlways{stmt.flp, VAlwaysKwd::CONT_ASSIGN, nullptr,
                                    new AstAssignW{stmt.flp, lhsp, rhsp}});
}

//######################################################################
// Entry point

//! Elaborate a generic-package instance into the table under the instance name.
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

void lowerPackages(const DesignLibrary& library, AstNetlist* rootp, PackageTable& table,
                   Design& design) {
    // The table under construction is also what a package may import: a package's `use`
    // clause names the packages elaborated before it, as for an entity or architecture
    Lowering lowering{rootp, &table.impl(), nullptr, &design};
    // A package that declares generics is a template: elaborate only its instances.
    const auto isTemplate = [](const PackageUnit& pkg) { return !pkg.generics.empty(); };
    for (const PackageUnit& pkg : library.packages)
        if (!pkg.isBody && !isTemplate(pkg)) lowering.emitPackage(pkg, table.impl());
    for (const PackageUnit& pkg : library.packages)
        if (pkg.isBody && !isTemplate(pkg)) lowering.emitPackage(pkg, table.impl());
    for (const PackageInstance& inst : library.packageInstances)
        lowering.instantiatePackage(library.packages, inst, table.impl());
    // A specification constant whose call had no body when the specification was emitted
    // is completed by a second pass over the specifications, now that every body is read
    for (int pass = 0; pass < 4 && lowering.hasDeferredConsts(); ++pass) {
        const std::string owner = lowering.deferredOwner();
        const auto it = std::find_if(
            library.packages.begin(), library.packages.end(),
            [&](const PackageUnit& pkg) { return !pkg.isBody && pkg.name == owner; });
        if (it == library.packages.end()) break;
        lowering.emitPackage(*it, table.impl());
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
        AstModule* const modp = new AstModule{entity.flp, entity.name, entity.libname};
        modp->inLibrary(entity.inLibrary);
        rootp->addModulesp(modp);
        modules[entity.name] = modp;
    }
}

//! Whether a record element's type is an unconstrained array (its width is set elsewhere)
static bool memberTypeUnconstrained(const TypeSpec& type) {
    const std::string& mark = type.mark;
    const bool unconstrainedMark
        = mark == "std_logic_vector" || mark == "std_ulogic_vector" || mark == "bit_vector"
          || mark == "unsigned" || mark == "signed" || mark == "u_unsigned" || mark == "u_signed";
    return unconstrainedMark && !type.hasRange && type.constraintMark.empty();
}

void collectUnconstrainedRecordMembers(const DesignLibrary& library, UnconstrainedRecordMap& out) {
    const auto scan = [&](const std::vector<TypeDecl>& types) {
        for (const TypeDecl& decl : types) {
            if (decl.kind != TypeDecl::RECORD) continue;
            std::set<std::string> members;
            for (const TypeDecl::Member& member : decl.members) {
                if (!memberTypeUnconstrained(member.type)) continue;
                for (const auto& name : member.names) members.insert(name.first);
            }
            if (!members.empty()) out[decl.name] = std::move(members);
        }
    };
    for (const PackageUnit& pkg : library.packages) scan(pkg.types);
    for (const EntityUnit& entity : library.entities) scan(entity.types);
}

//! Record the physical unit names of every package and entity, each with the multiple of
//! its type's step unit. A generic default is lowered before the declarations of the unit
//! that owns it, so a literal's unit has to be findable among types not elaborated yet.
void collectPhysicalUnits(const DesignLibrary& library, Design& design) {
    const auto scan = [&design](const std::vector<TypeDecl>& types) {
        for (const TypeDecl& decl : types) {
            if (decl.kind == TypeDecl::PHYSICAL) addPhysicalUnits(decl, design);
        }
    };
    for (const PackageUnit& pkg : library.packages) scan(pkg.types);
    for (const EntityUnit& entity : library.entities) scan(entity.types);
}

bool recordPortFullyConstrained(const PortDecl& port, const UnconstrainedRecordMap& members) {
    const auto it = members.find(port.type.mark);
    if (it == members.end()) return false;
    for (const std::string& member : it->second) {
        const bool named
            = std::any_of(port.type.memberConstraints.begin(), port.type.memberConstraints.end(),
                          [&](const TypeSpec::MemberConstraint& constraint) {
                              return constraint.name == member;
                          });
        if (!named) return false;
    }
    return true;
}

bool hasUnconstrainedPorts(const EntityUnit& entity, const UnconstrainedRecordMap& members) {
    for (const PortDecl& port : entity.ports) {
        const std::string& mark = port.type.mark;
        const bool unconstrainedMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                                       || mark == "bit_vector" || mark == "unsigned"
                                       || mark == "signed";
        if (unconstrainedMark && !port.type.hasRange && port.type.constraintMark.empty())
            return true;
        if (!port.type.hasRange && port.type.constraintMark.empty() && members.count(mark)
            && !recordPortFullyConstrained(port, members))
            return true;
    }
    return false;
}

//! Report why an unconstrained-port entity with no instance cannot be modelled, at the
//! offending port, matching the message a top-level use of the type would give
void reportUnspecializedPorts(const EntityUnit& entity, const UnconstrainedRecordMap& members) {
    for (const PortDecl& port : entity.ports) {
        const std::string& mark = port.type.mark;
        const bool unconstrainedMark = mark == "std_logic_vector" || mark == "std_ulogic_vector"
                                       || mark == "bit_vector" || mark == "unsigned"
                                       || mark == "signed";
        if ((!port.type.hasRange && port.type.constraintMark.empty()
             && (unconstrainedMark
                 || (members.count(mark) && !recordPortFullyConstrained(port, members))))) {
            unsupportedError(port.flp, "unconstrained type '" + mark + "'");
            return;
        }
    }
}

bool lower(const EntityUnit& entity, const Architecture& arch, AstModule* modp,
           const PackageTable& table, const ModuleMap& modules, Design& design,
           const PortBoundsMap* portBounds) {
    Lowering lowering{rootpFromModules(modules), &table.impl(), &modules, &design};
    return lowering.emit(entity, arch, modp, nullptr, portBounds);
}

}  // namespace vhdlnative
