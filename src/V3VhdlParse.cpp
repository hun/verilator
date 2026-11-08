// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL frontend driver
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
// WP0 frontend: lexes and parses entities with a port clause, plus the context clauses
// (`library`/`use`/`context`) needed to skip normal VHDL file headers. Architectures,
// packages and everything else report "not supported yet". Entities are emitted as empty
// AstModules with AstVar ports, which is enough to validate the seam end-to-end.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlParse.h"

#include "V3Error.h"
#include "V3File.h"
#include "V3FileLine.h"
#include "V3Global.h"
#include "V3Os.h"
#include "V3VhdlXml.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

VL_DEFINE_DEBUG_FUNCTIONS;

namespace {

//######################################################################
// Frontend design library

struct VhdlPort final {
    std::string name;
    VDirection dir = VDirection::INPUT;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    bool isVector = false;
    int left = 0;
    int right = 0;
    bool ascending = true;
    VSigning signing = VSigning::UNSIGNED;
    FileLine* flp = nullptr;
};

struct VhdlEntity final {
    std::string name;
    std::string libname;
    bool inLibrary = false;
    FileLine* flp = nullptr;
    std::vector<VhdlPort> ports;
};

struct VhdlArchitecture final {
    std::string name;
    std::string entityName;
    std::string libname;
    FileLine* flp = nullptr;
};

//! One VHDL source file handed to the GHDL producer
struct VhdlFile final {
    std::string filename;  // Resolved path, as passed to GHDL
    std::string libname;  // Library the file was requested in
    bool inLibrary = false;  // File came from a -v/--library file
};

}  // namespace

//######################################################################
// Implementation state: the frontend design library and AST emission

class V3VhdlParseImp final {
public:
    AstNetlist* const m_rootp;  // Netlist to emit into
    std::vector<VhdlEntity> m_entities;  // Collected entities
    std::vector<VhdlArchitecture> m_architectures;  // Collected architectures
    std::string m_libname;  // Library of current file
    bool m_inLibrary = false;  // Current file is a library file
    std::vector<VhdlFile> m_files;  // Input files for the GHDL producer

    explicit V3VhdlParseImp(AstNetlist* rootp)
        : m_rootp{rootp} {}
    ~V3VhdlParseImp() = default;

    const std::string& currentLibname() const { return m_libname; }
    bool currentInLibrary() const { return m_inLibrary; }
    void addEntity(VhdlEntity&& entity) { m_entities.push_back(std::move(entity)); }
    void addArchitecture(VhdlArchitecture&& architecture) {
        m_architectures.push_back(std::move(architecture));
    }

    AstNodeDType* makeDType(const VhdlPort& port) const {
        AstTypeTable* const tablep = m_rootp->typeTablep();
        if (!port.isVector) {
            if (port.kwd == VBasicDTypeKwd::BIT) {
                return tablep->findBasicDType(port.flp, VBasicDTypeKwd::BIT);
            }
            if (port.kwd == VBasicDTypeKwd::INT) {
                return tablep->findBasicDType(port.flp, VBasicDTypeKwd::INT);
            }
            return tablep->findBasicDType(port.flp, VBasicDTypeKwd::LOGIC);
        }
        const int width = std::abs(port.left - port.right) + 1;
        const VNumRange range{port.left, port.right, port.ascending};
        return tablep->findLogicBitDType(port.flp, VBasicDTypeKwd::LOGIC, range, width,
                                         port.signing);
    }

    void emitEntity(const VhdlEntity& entity) const {
        AstModule* const modp = new AstModule{entity.flp, entity.name, entity.libname};
        modp->inLibrary(entity.inLibrary);
        int pinNum = 0;
        for (const VhdlPort& port : entity.ports) {
            AstVar* const varp = new AstVar{port.flp, VVarType::PORT, port.name, makeDType(port)};
            varp->direction(port.dir);
            varp->pinNum(++pinNum);
            modp->addStmtsp(varp);
        }
        m_rootp->addModulesp(modp);
    }

    static std::string designUnitKey(const std::string& libname, const std::string& name) {
        return libname + "." + name;
    }

    //! Run GHDL on the collected files, then emit AstModules from its XML
    void finishXml();

    //! Emit design units collected by the in-tree parser. Reserved for that parser; the active
    //! frontend is finishXml().
    void finishLegacy() {
        // Bind each entity to its architecture. Until configurations are supported, the last
        // analyzed architecture is the default (see VHDL_FRONTEND_PLAN.md section 7).
        std::map<std::string, const VhdlArchitecture*> archByEntity;
        for (const VhdlArchitecture& arch : m_architectures) {
            const std::string key = designUnitKey(arch.libname, arch.entityName);
            if (archByEntity.find(key) != archByEntity.end()) {
                arch.flp->v3warn(E_UNSUPPORTED, "Multiple architectures for entity '"
                                                    << arch.entityName
                                                    << "'; using the last analyzed one");
            }
            archByEntity[key] = &arch;
        }

        std::set<std::string> entityKeys;
        for (const VhdlEntity& entity : m_entities) {
            const std::string key = designUnitKey(entity.libname, entity.name);
            if (!entityKeys.insert(key).second) {
                entity.flp->v3error("Duplicate VHDL entity: '" + entity.name + "' in library '"
                                    + entity.libname + "'");
                continue;
            }
            if (archByEntity.find(key) == archByEntity.end()) {
                entity.flp->v3error("Entity '" + entity.name
                                    + "' has no architecture; cannot elaborate it");
                continue;
            }
            UINFO(4, "VHDL emit entity " << entity.name);
            emitEntity(entity);
        }

        // An architecture must name an entity in the same library
        for (const VhdlArchitecture& arch : m_architectures) {
            if (entityKeys.find(designUnitKey(arch.libname, arch.entityName))
                == entityKeys.end()) {
                arch.flp->v3error("Architecture '" + arch.name + "' names unknown entity '"
                                  + arch.entityName + "'");
            }
        }

        m_entities.clear();
        m_architectures.clear();
    }
};

namespace {

//######################################################################
// GHDL XML -> Verilator AST

//! VHDL identifiers are case-insensitive; lowercase is the lookup key
std::string vhdlFold(const std::string& name) {
    std::string folded{name};
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return folded;
}

//! Direct `<el>` children of a GHDL chain/list element
std::vector<const V3VhdlXmlNode*> childEls(const V3VhdlXmlNode* chainp) {
    std::vector<const V3VhdlXmlNode*> result;
    if (!chainp) return result;
    for (const std::unique_ptr<V3VhdlXmlNode>& childp : chainp->children()) {
        if (childp->name() == "el") result.push_back(childp.get());
    }
    return result;
}

//! VHDL type information needed to build one Verilator dtype
struct VhdlDTypeSpec final {
    bool isVector = false;
    bool isInteger = false;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    int left = 0;
    int right = 0;
    bool ascending = false;
};

class VhdlXmlFrontend final {
    using Node = V3VhdlXmlNode;

    AstNetlist* const m_rootp;  // Netlist to emit into
    const Node* const m_docp;  // Parsed GHDL XML root
    const std::vector<VhdlFile>& m_files;  // Files handed to GHDL
    std::map<std::string, const Node*> m_byId;  // XML nodes indexed by `id`
    std::vector<const Node*> m_entityNodes;  // User entity declarations
    std::vector<const Node*> m_archNodes;  // User architecture bodies
    std::map<std::string, AstVar*> m_scope;  // Current module: folded name -> Var
    AstModule* m_modp = nullptr;  // Module being emitted

    // ---- XML access

    FileLine* flpOf(const Node* nodep) const {
        const std::string& file = nodep->attr("file");
        FileLine* const flp = new FileLine{file.empty() ? "<ghdl>" : file};
        const std::string& line = nodep->attr("line");
        if (!line.empty()) flp->lineno(std::atoi(line.c_str()));
        return flp;
    }
    void error(const Node* nodep, const std::string& message) const {
        flpOf(nodep)->v3error(message);
    }
    void unsupported(const Node* nodep, const std::string& what) const {
        error(nodep, "VHDL " + what + " is not supported yet");
    }
    const Node* nodeById(const std::string& id) const {
        const auto it = m_byId.find(id);
        return it == m_byId.end() ? nullptr : it->second;
    }

    void index(const Node* nodep) {
        const std::string& id = nodep->attr("id");
        if (!id.empty()) m_byId.emplace(id, nodep);
        for (const std::unique_ptr<Node>& childp : nodep->children()) index(childp.get());
    }

    bool isUserFile(const Node* nodep) const {
        const std::string& file = nodep->attr("file");
        for (const VhdlFile& vfile : m_files) {
            if (vfile.filename == file) return true;
        }
        return false;
    }

    void collect(const Node* nodep) {
        const std::string& kind = nodep->attr("kind");
        if (kind == "entity_declaration" && isUserFile(nodep)) {
            m_entityNodes.push_back(nodep);
        } else if (kind == "architecture_body" && isUserFile(nodep)) {
            m_archNodes.push_back(nodep);
        }
        for (const std::unique_ptr<Node>& childp : nodep->children()) collect(childp.get());
    }

    std::string entityNameOf(const Node* archp) const {
        const Node* const namep = archp->child("entity_name");
        if (!namep) return {};
        std::string name = namep->attr("identifier");
        if (name.empty()) {
            if (const Node* const refp = namep->child("named_entity")) {
                if (const Node* const entityp = nodeById(refp->attr("ref"))) {
                    name = entityp->attr("identifier");
                }
            }
        }
        return name;
    }

    // ---- Types

    //! Read an integer-literal node's value; false for non-literals (e.g. generic expressions)
    bool literalValue(const Node* nodep, int& value) const {
        if (!nodep || nodep->attr("kind") != "integer_literal") return false;
        value = static_cast<int>(std::strtol(nodep->attr("value").c_str(), nullptr, 10));
        return true;
    }

    VhdlDTypeSpec parseType(const Node* subtypep) const {
        VhdlDTypeSpec spec;
        const std::string kind = subtypep->attr("kind");
        if (kind == "simple_name") {
            const std::string id = vhdlFold(subtypep->attr("identifier"));
            if (id == "std_logic" || id == "boolean") {
                spec.kwd = VBasicDTypeKwd::LOGIC;
            } else if (id == "bit") {
                spec.kwd = VBasicDTypeKwd::BIT;
            } else if (id == "integer" || id == "natural" || id == "positive") {
                spec.isInteger = true;
                spec.kwd = VBasicDTypeKwd::INT;
            } else {
                error(subtypep,
                      "VHDL type '" + subtypep->attr("identifier") + "' is not supported yet");
            }
            return spec;
        }
        if (kind != "array_subtype_definition") {
            error(subtypep, "VHDL type form '" + kind + "' is not supported yet");
            return spec;
        }

        const Node* const markp = subtypep->child("subtype_type_mark");
        const std::string mark = markp ? vhdlFold(markp->attr("identifier")) : std::string{};
        if (mark == "signed") {
            spec.kwd = VBasicDTypeKwd::LOGIC;
            spec.signing = VSigning::SIGNED;
        } else if (mark == "unsigned" || mark == "std_logic_vector") {
            spec.kwd = VBasicDTypeKwd::LOGIC;
            spec.signing = VSigning::UNSIGNED;
        } else if (mark == "bit_vector") {
            spec.kwd = VBasicDTypeKwd::BIT;
        } else {
            error(subtypep, "VHDL array type '" + mark + "' is not supported yet");
            return spec;
        }
        spec.isVector = true;

        const Node* const indicesp = subtypep->child("index_constraint_list");
        if (!indicesp) {
            error(subtypep, "unconstrained VHDL array is not supported yet");
            return spec;
        }
        const std::vector<const Node*> indices = childEls(indicesp);
        if (indices.size() != 1) {
            error(subtypep, "multi-dimensional VHDL arrays are not supported yet");
            return spec;
        }
        const Node* const rangep = indices.front()->child("range_constraint");
        if (!rangep) {
            error(subtypep, "VHDL array without a range constraint");
            return spec;
        }
        spec.ascending = rangep->attr("direction") == "to";
        if (!literalValue(rangep->child("left_limit_expr"), spec.left)
            || !literalValue(rangep->child("right_limit_expr"), spec.right)) {
            error(rangep, "non-literal VHDL range limit is not supported yet");
            spec.isVector = false;  // Keep the tree buildable; the error aborts the run
            return spec;
        }
        return spec;
    }

    AstNodeDType* dtypeOf(const VhdlDTypeSpec& spec, FileLine* flp) const {
        AstTypeTable* const tablep = m_rootp->typeTablep();
        if (spec.isInteger) return tablep->findBasicDType(flp, VBasicDTypeKwd::INT);
        if (!spec.isVector) return tablep->findBasicDType(flp, spec.kwd);
        const int width = std::abs(spec.left - spec.right) + 1;
        return tablep->findLogicBitDType(
            flp, spec.kwd, VNumRange{spec.left, spec.right, spec.ascending}, width, spec.signing);
    }

    AstNodeDType* bitDType(FileLine* flp) const {
        return m_rootp->typeTablep()->findBasicDType(flp, VBasicDTypeKwd::LOGIC);
    }
    AstNodeDType* intDType(FileLine* flp) const {
        return m_rootp->typeTablep()->findBasicDType(flp, VBasicDTypeKwd::INT);
    }

    AstConst* makeConst(FileLine* flp, AstNodeDType* dtypep, uint64_t value) const {
        V3Number num{flp, dtypep->width(), static_cast<uint32_t>(value)};
        AstConst* const constp = new AstConst{flp, num};
        constp->dtypep(dtypep);
        return constp;
    }

    // ---- Names

    AstVar* lookupVar(const Node* namep) const {
        const auto it = m_scope.find(vhdlFold(namep->attr("identifier")));
        return it == m_scope.end() ? nullptr : it->second;
    }

    // ---- Expressions

    AstNodeExpr* lowerExpr(const Node* nodep, AstNodeDType* contextp) {
        if (!nodep) return nullptr;
        FileLine* const flp = flpOf(nodep);
        const std::string kind = nodep->attr("kind");

        if (kind == "simple_name") {
            AstVar* const varp = lookupVar(nodep);
            if (!varp) {
                error(nodep, "Unknown VHDL identifier '" + nodep->attr("identifier") + "'");
                return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
            }
            return new AstVarRef{flp, varp, VAccess::READ};
        }
        if (kind == "integer_literal") {
            const long value = std::strtol(nodep->attr("value").c_str(), nullptr, 10);
            return makeConst(flp, contextp ? contextp : intDType(flp),
                             static_cast<uint32_t>(value));
        }
        if (kind == "character_literal") {
            const std::string id = nodep->attr("identifier");
            if (id == "'0'" || id == "'1'") {
                return makeConst(flp, contextp ? contextp : bitDType(flp), id == "'1'" ? 1U : 0U);
            }
            unsupported(nodep, "character literal " + id);
            return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
        }
        if (kind == "addition_operator" || kind == "subtraction_operator" || kind == "and_operator"
            || kind == "or_operator" || kind == "xor_operator") {
            AstNodeExpr* const lhsp = lowerExpr(nodep->child("left"), contextp);
            if (!lhsp) return nullptr;
            AstNodeDType* const dtp = lhsp->dtypep();
            AstNodeExpr* const rhsp = lowerExpr(nodep->child("right"), dtp);
            if (!rhsp) return nullptr;
            AstNodeExpr* resultp = nullptr;
            if (kind == "addition_operator") {
                resultp = new AstAdd{flp, lhsp, rhsp};
            } else if (kind == "subtraction_operator") {
                resultp = new AstSub{flp, lhsp, rhsp};
            } else if (kind == "and_operator") {
                resultp = new AstAnd{flp, lhsp, rhsp};
            } else if (kind == "or_operator") {
                resultp = new AstOr{flp, lhsp, rhsp};
            } else {
                resultp = new AstXor{flp, lhsp, rhsp};
            }
            resultp->dtypep(dtp);
            return resultp;
        }
        if (kind == "not_operator") {
            AstNodeExpr* const operandp = lowerExpr(nodep->child("operand"), contextp);
            if (!operandp) return nullptr;
            AstNot* const notp = new AstNot{flp, operandp};
            notp->dtypep(operandp->dtypep());
            return notp;
        }
        if (kind == "equality_operator" || kind == "inequality_operator") {
            AstNodeExpr* const lhsp = lowerExpr(nodep->child("left"), nullptr);
            if (!lhsp) return nullptr;
            AstNodeExpr* const rhsp = lowerExpr(nodep->child("right"), lhsp->dtypep());
            if (!rhsp) return nullptr;
            AstNodeExpr* const resultp
                = kind == "equality_operator"
                      ? static_cast<AstNodeExpr*>(new AstEq{flp, lhsp, rhsp})
                      : static_cast<AstNodeExpr*>(new AstNeq{flp, lhsp, rhsp});
            resultp->dtypep(bitDType(flp));
            return resultp;
        }
        if (kind == "type_conversion") {
            AstNodeExpr* const innerp = lowerExpr(nodep->child("expression"), contextp);
            if (!innerp) return nullptr;
            if (!contextp || innerp->dtypep() == contextp) return innerp;
            return new AstCast{flp, innerp, contextp};
        }
        if (kind == "function_call") {
            const Node* const prefixp = nodep->child("prefix");
            const std::string fn = prefixp ? prefixp->attr("identifier") : std::string{};
            unsupported(nodep, "function call '" + fn + "'");
            return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
        }
        unsupported(nodep, "expression '" + kind + "'");
        return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
    }

    AstNodeExpr* lowerDefault(const Node* defp, AstNodeDType* dtypep) {
        const std::string kind = defp->attr("kind");
        if (kind != "aggregate") return lowerExpr(defp, dtypep);

        const std::vector<const Node*> choices
            = childEls(defp->child("association_choices_chain"));
        if (choices.size() != 1) {
            unsupported(defp, "aggregate with multiple choices");
            return nullptr;
        }
        const Node* const exprp = choices.front()->child("associated_expr");
        if (!exprp) {
            unsupported(defp, "aggregate without an associated expression");
            return nullptr;
        }
        const std::string id = exprp->attr("identifier");
        if (id == "'0'" || id == "'1'") {
            uint32_t value = 0;
            if (id == "'1'") {
                value = dtypep->width() >= 32 ? 0xffffffffU : ((1U << dtypep->width()) - 1U);
            }
            return makeConst(flpOf(defp), dtypep, value);
        }
        unsupported(exprp, "aggregate element " + id);
        return nullptr;
    }

    // ---- Statements

    AstNode* lowerAssignment(const Node* stmtp, bool delayed) {
        FileLine* const flp = flpOf(stmtp);
        const Node* const targetp = stmtp->child("target");
        if (!targetp || targetp->attr("kind") != "simple_name") {
            unsupported(stmtp, "assignment target form");
            return nullptr;
        }
        AstVar* const varp = lookupVar(targetp);
        if (!varp) {
            error(targetp, "Unknown VHDL signal '" + targetp->attr("identifier") + "'");
            return nullptr;
        }
        const std::vector<const Node*> waves = childEls(stmtp->child("waveform_chain"));
        if (waves.size() != 1) {
            unsupported(stmtp, "multiple waveform elements");
            return nullptr;
        }
        const Node* const wep = waves.front()->child("we_value");
        if (!wep) {
            unsupported(stmtp, "assignment without a waveform value");
            return nullptr;
        }
        AstNodeExpr* const lhsp = new AstVarRef{flp, varp, VAccess::WRITE};
        AstNodeExpr* const rhsp = lowerExpr(wep, varp->dtypep());
        if (!rhsp) return nullptr;
        if (delayed) return new AstAssignDly{flp, lhsp, rhsp};
        return new AstAssignW{flp, lhsp, rhsp};
    }

    AstNode* lowerStmt(const Node* stmtp, bool delayed) {
        const std::string kind = stmtp->attr("kind");
        if (kind == "simple_signal_assignment_statement") {
            return lowerAssignment(stmtp, delayed);
        }
        if (kind == "null_statement") return nullptr;
        if (kind == "if_statement") {
            FileLine* const flp = flpOf(stmtp);
            if (stmtp->child("else_clause")) unsupported(stmtp, "else branch");
            AstNodeExpr* const condp = lowerExpr(stmtp->child("condition"), bitDType(flp));
            AstNode* const thensp
                = lowerStmtChain(stmtp->child("sequential_statement_chain"), delayed);
            return new AstIf{flp, condp, thensp};
        }
        unsupported(stmtp, "statement '" + kind + "'");
        return nullptr;
    }

    AstNode* lowerStmtChain(const Node* chainp, bool delayed) {
        AstNode* headp = nullptr;
        for (const Node* const stmtp : childEls(chainp)) {
            AstNode* const nodep = lowerStmt(stmtp, delayed);
            if (!nodep) continue;
            headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
        }
        return headp;
    }

    const Node* singleTopIf(const Node* chainp) const {
        const std::vector<const Node*> elsp = childEls(chainp);
        if (elsp.size() != 1) return nullptr;
        return elsp.front()->attr("kind") == "if_statement" ? elsp.front() : nullptr;
    }

    AstNodeExpr* singleActual(const Node* callp) {
        const std::vector<const Node*> argsp
            = childEls(callp->child("parameter_association_chain"));
        if (argsp.size() != 1) {
            unsupported(callp,
                        "edge function with " + std::to_string(argsp.size()) + " arguments");
            return nullptr;
        }
        const Node* const actualp = argsp.front()->child("actual");
        if (!actualp) {
            unsupported(callp, "edge function without an actual");
            return nullptr;
        }
        return lowerExpr(actualp, nullptr);
    }

    void emitConcurrentAssignment(const Node* processp) {
        AstNode* const bodyp
            = lowerStmtChain(processp->child("sequential_statement_chain"), false);
        if (!bodyp) return;
        m_modp->addStmtsp(new AstAlways{flpOf(processp), VAlwaysKwd::CONT_ASSIGN, nullptr, bodyp});
    }

    void emitProcess(const Node* processp) {
        FileLine* const flp = flpOf(processp);
        const Node* const bodyp = processp->child("sequential_statement_chain");

        // Clocked idiom: a single "if rising_edge(clk)"/"if falling_edge(clk)" wrapper
        if (const Node* const ifp = singleTopIf(bodyp)) {
            const Node* const condp = ifp->child("condition");
            if (condp && condp->attr("kind") == "function_call") {
                const Node* const prefixp = condp->child("prefix");
                const std::string fn
                    = prefixp ? vhdlFold(prefixp->attr("identifier")) : std::string{};
                if (fn == "rising_edge" || fn == "falling_edge") {
                    if (ifp->child("else_clause")) {
                        unsupported(ifp, "else branch in a clocked process");
                        return;
                    }
                    AstNodeExpr* const sensp = singleActual(condp);
                    if (!sensp) return;
                    AstSenItem* const itemp = new AstSenItem{
                        flp, fn == "rising_edge" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE,
                        sensp};
                    AstNode* const stmtsp
                        = lowerStmtChain(ifp->child("sequential_statement_chain"), true);
                    m_modp->addStmtsp(new AstAlways{flp, VAlwaysKwd::ALWAYS,
                                                    new AstSenTree{flp, itemp}, stmtsp});
                    return;
                }
            }
        }

        // Plain process: trigger on every sensitivity item
        AstSenItem* itemsp = nullptr;
        if (const Node* const sensp = processp->child("sensitivity_list")) {
            for (const Node* const elp : childEls(sensp)) {
                const Node* const namep
                    = elp->attr("ref").empty() ? elp : nodeById(elp->attr("ref"));
                if (!namep) {
                    error(elp, "Dangling sensitivity-list reference");
                    continue;
                }
                AstNodeExpr* const exprp = lowerExpr(namep, nullptr);
                if (!exprp) continue;
                AstSenItem* const itemp = new AstSenItem{flp, VEdgeType::ET_CHANGED, exprp};
                itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
            }
        }
        if (!itemsp) {
            unsupported(processp, "process without a sensitivity list");
            return;
        }
        AstNode* const stmtsp = lowerStmtChain(bodyp, true);
        m_modp->addStmtsp(
            new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemsp}, stmtsp});
    }

    void lowerConcurrent(const Node* stmtp) {
        const std::string kind = stmtp->attr("kind");
        if (kind == "sensitized_process_statement") {
            if (stmtp->child("process_origin")) {
                emitConcurrentAssignment(stmtp);
            } else {
                emitProcess(stmtp);
            }
            return;
        }
        unsupported(stmtp, "concurrent statement '" + kind + "'");
    }

    // ---- Module emission

    VDirection directionOf(const std::string& mode) const {
        if (mode == "in") return VDirection::INPUT;
        if (mode == "out") return VDirection::OUTPUT;
        if (mode == "inout") return VDirection::INOUT;
        if (mode == "buffer") return VDirection::OUTPUT;
        return VDirection::INPUT;
    }

    void emitModule(const Node* entityp, const Node* archp) {
        FileLine* const flp = flpOf(entityp);
        m_modp = new AstModule{flp, entityp->attr("identifier"), "work"};
        m_scope.clear();

        int pinNum = 0;
        if (const Node* const portsp = entityp->child("port_chain")) {
            for (const Node* const portp : childEls(portsp)) {
                if (portp->attr("kind") != "interface_signal_declaration") {
                    unsupported(portp, "port kind '" + portp->attr("kind") + "'");
                    continue;
                }
                const Node* const subtypep = portp->child("subtype_indication");
                if (!subtypep) {
                    error(portp, "VHDL port without a subtype");
                    continue;
                }
                FileLine* const pflp = flpOf(portp);
                const std::string pname = portp->attr("identifier");
                AstVar* const varp
                    = new AstVar{pflp, VVarType::PORT, pname, dtypeOf(parseType(subtypep), pflp)};
                varp->direction(directionOf(portp->attr("mode")));
                varp->pinNum(++pinNum);
                // VHDL ports are signals, so procedural assignment is legal
                varp->declTyped(true);
                m_modp->addStmtsp(varp);
                m_scope[vhdlFold(pname)] = varp;
            }
        }

        if (const Node* const declsp = archp->child("declaration_chain")) {
            for (const Node* const declp : childEls(declsp)) {
                const std::string kind = declp->attr("kind");
                if (kind != "signal_declaration") {
                    unsupported(declp, "declaration '" + kind + "'");
                    continue;
                }
                const Node* const subtypep = declp->child("subtype_indication");
                if (!subtypep) {
                    error(declp, "VHDL signal without a subtype");
                    continue;
                }
                FileLine* const dflp = flpOf(declp);
                const std::string name = declp->attr("identifier");
                AstVar* const varp
                    = new AstVar{dflp, VVarType::VAR, name, dtypeOf(parseType(subtypep), dflp)};
                m_modp->addStmtsp(varp);
                m_scope[vhdlFold(name)] = varp;

                if (const Node* const defp = declp->child("default_value")) {
                    varp->noCReset(true);
                    AstNodeExpr* const rhsp = lowerDefault(defp, varp->dtypep());
                    if (rhsp) {
                        AstVarRef* const lhsp = new AstVarRef{dflp, varp, VAccess::WRITE};
                        m_modp->addStmtsp(
                            new AstInitialStatic{dflp, new AstAssign{dflp, lhsp, rhsp}});
                    }
                }
            }
        }

        if (const Node* const stmtsp = archp->child("concurrent_statement_chain")) {
            for (const Node* const stmtp : childEls(stmtsp)) lowerConcurrent(stmtp);
        }

        m_rootp->addModulesp(m_modp);
        m_modp = nullptr;
    }

public:
    VhdlXmlFrontend(AstNetlist* rootp, const Node* docp, const std::vector<VhdlFile>& files)
        : m_rootp{rootp}
        , m_docp{docp}
        , m_files{files} {}
    ~VhdlXmlFrontend() = default;

    void emit() {
        index(m_docp);
        collect(m_docp);

        // Bind each entity to one architecture; last analyzed wins until configurations exist
        std::map<std::string, const Node*> archByEntity;
        for (const Node* const archp : m_archNodes) {
            const std::string entityName = entityNameOf(archp);
            if (entityName.empty()) {
                error(archp, "VHDL architecture without an entity name");
                continue;
            }
            const std::string key = vhdlFold(entityName);
            if (archByEntity.find(key) != archByEntity.end()) {
                flpOf(archp)->v3warn(E_UNSUPPORTED, "Multiple architectures for entity '"
                                                        << entityName << "'; using the last one");
            }
            archByEntity[key] = archp;
        }

        std::set<std::string> entityKeys;
        for (const Node* const entityp : m_entityNodes) {
            const std::string name = entityp->attr("identifier");
            if (!entityKeys.insert(vhdlFold(name)).second) {
                error(entityp, "Duplicate VHDL entity: '" + name + "'");
                continue;
            }
            const auto it = archByEntity.find(vhdlFold(name));
            if (it == archByEntity.end()) {
                error(entityp, "Entity '" + name + "' has no architecture; cannot elaborate it");
                continue;
            }
            emitModule(entityp, it->second);
        }

        for (const Node* const archp : m_archNodes) {
            const std::string entityName = entityNameOf(archp);
            if (!entityName.empty() && !entityKeys.count(vhdlFold(entityName))) {
                error(archp, "Architecture '" + archp->attr("identifier")
                                 + "' names unknown entity '" + entityName + "'");
            }
        }
    }
};

}  // namespace

void V3VhdlParseImp::finishXml() {
    if (m_files.empty()) return;

    std::string ghdl = "ghdl";
    if (const char* const envp = std::getenv("VERILATOR_GHDL")) {
        if (*envp) ghdl = envp;
    }

    V3File::createMakeDir();
    const std::string makeDir = v3Global.opt.makeDir();
    const std::string xmlFile
        = V3Os::filenameJoin({makeDir, v3Global.opt.prefix() + "__vhdl.xml"});
    const std::string errFile
        = V3Os::filenameJoin({makeDir, v3Global.opt.prefix() + "__vhdl.err"});
    const auto quotePath = [](const std::string& path) { return "\"" + path + "\""; };

    std::string command = quotePath(ghdl);
    command += " --file-to-xml --std=08 --workdir=" + quotePath(makeDir);
    for (const VhdlFile& vfile : m_files) command += " " + quotePath(vfile.filename);
    command += " > " + quotePath(xmlFile) + " 2> " + quotePath(errFile);

    UINFO(2, "Running GHDL: " << command);
    const int status = V3Os::system(command);
    if (status != 0) {
        std::ifstream eifs{errFile};
        const std::string errText{std::istreambuf_iterator<char>{eifs},
                                  std::istreambuf_iterator<char>{}};
        v3Global.rootp()->fileline()->v3error(
            "GHDL failed to analyze the VHDL sources (exit "
            << status
            << "). Verilator uses GHDL as its VHDL frontend; install GHDL or set "
               "VERILATOR_GHDL to its executable.\n"
            << errText);
        std::remove(errFile.c_str());
        std::remove(xmlFile.c_str());
        return;
    }

    std::ifstream ifs{xmlFile};
    if (!ifs) {
        v3Global.rootp()->fileline()->v3error("Cannot read GHDL XML output: '" + xmlFile + "'");
        std::remove(errFile.c_str());
        return;
    }
    const std::string text{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
    ifs.close();
    std::remove(xmlFile.c_str());
    std::remove(errFile.c_str());

    FileLine* const flp = new FileLine{"<ghdl>"};
    const std::unique_ptr<V3VhdlXmlNode> docp = V3VhdlXml::parse(flp, text);
    if (!docp) return;
    VhdlXmlFrontend frontend{m_rootp, docp.get(), m_files};
    frontend.emit();
}

namespace {

//######################################################################
// Lexer

class VhdlToken final {
public:
    enum Kind : uint8_t { ID, NUMBER, SYMBOL, END };
    Kind kind = END;
    char sym = 0;
    std::string text;
    FileLine* flp = nullptr;
};

class VhdlLexer final {
    const std::string m_filename;
    const std::string m_text;
    size_t m_pos = 0;
    int m_line = 1;

    FileLine* newFileLine() const {
        FileLine* const flp = new FileLine{m_filename};
        flp->lineno(m_line);
        flp->applyIgnores();  // Respect .vlt lint waivers
        return flp;
    }

    static bool isIdStart(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    static bool isIdChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

public:
    VhdlLexer(const std::string& filename, std::string text)
        : m_filename{filename}
        , m_text{std::move(text)} {}
    ~VhdlLexer() = default;

    VhdlToken next() {
        // Skip whitespace and line comments
        for (;;) {
            if (m_pos >= m_text.size()) break;
            const char c = m_text[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                ++m_pos;
            } else if (c == '-' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '-') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
            } else {
                break;
            }
        }

        VhdlToken tok;
        tok.flp = newFileLine();
        if (m_pos >= m_text.size()) {
            tok.kind = VhdlToken::END;
            return tok;
        }

        const char c = m_text[m_pos];
        if (isIdStart(c)) {
            const size_t start = m_pos;
            while (m_pos < m_text.size() && isIdChar(m_text[m_pos])) ++m_pos;
            tok.kind = VhdlToken::ID;
            tok.text = m_text.substr(start, m_pos - start);
            // VHDL identifiers are case-insensitive; use lowercase as the canonical form
            std::transform(tok.text.begin(), tok.text.end(), tok.text.begin(), [](char ch) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            });
            return tok;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const size_t start = m_pos;
            while (m_pos < m_text.size()
                   && (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))
                       || m_text[m_pos] == '_')) {
                ++m_pos;
            }
            tok.kind = VhdlToken::NUMBER;
            tok.text = m_text.substr(start, m_pos - start);
            tok.text.erase(std::remove(tok.text.begin(), tok.text.end(), '_'), tok.text.end());
            return tok;
        }
        if (c == '(' || c == ')' || c == ';' || c == ':' || c == ',' || c == '.') {
            tok.kind = VhdlToken::SYMBOL;
            tok.sym = c;
            ++m_pos;
            return tok;
        }

        // Not supported yet; report and skip so parsing can continue to a cleaner error
        if (c == '\'') {
            tok.flp->v3error("VHDL character literals and attributes are not supported yet");
            ++m_pos;
            if (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
            return next();
        }
        if (c == '"') {
            tok.flp->v3error("VHDL string literals are not supported yet");
            ++m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '"' && m_text[m_pos] != '\n') {
                ++m_pos;
            }
            if (m_pos < m_text.size() && m_text[m_pos] == '"') ++m_pos;
            return next();
        }
        if (c == '\\') {
            tok.flp->v3error("VHDL extended identifiers are not supported yet");
            ++m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '\\') ++m_pos;
            if (m_pos < m_text.size()) ++m_pos;
            return next();
        }

        tok.flp->v3error(std::string{"Unexpected character '"} + c + "' in VHDL source");
        ++m_pos;
        return next();
    }
};

//######################################################################
// Parser

class VhdlParser final {
    V3VhdlParseImp* const m_impp;  // Owner
    VhdlLexer m_lexer;  // Token source
    VhdlToken m_cur;  // Current token
    bool m_failed = false;  // Hit an error; stop parsing this file

    void advance() { m_cur = m_lexer.next(); }

    bool isId(const char* text) const { return m_cur.kind == VhdlToken::ID && m_cur.text == text; }
    bool acceptId(const char* text) {
        if (!isId(text)) return false;
        advance();
        return true;
    }
    bool acceptSym(char sym) {
        if (m_cur.kind != VhdlToken::SYMBOL || m_cur.sym != sym) return false;
        advance();
        return true;
    }
    void error(const std::string& msg) { errorAt(m_cur.flp, msg); }
    void errorAt(FileLine* flp, const std::string& msg) {
        flp->v3error(msg);
        m_failed = true;
    }
    std::string tokenText() const {
        if (m_cur.kind == VhdlToken::ID || m_cur.kind == VhdlToken::NUMBER) return m_cur.text;
        if (m_cur.kind == VhdlToken::SYMBOL) return std::string{1, m_cur.sym};
        return "<end of file>";
    }
    std::string expectId() {
        if (m_cur.kind != VhdlToken::ID) {
            error("Expected an identifier, got '" + tokenText() + "'");
            return "";
        }
        std::string name = m_cur.text;
        advance();
        return name;
    }
    int expectNumber() {
        if (m_cur.kind != VhdlToken::NUMBER) {
            error("Expected an integer literal, got '" + tokenText() + "'");
            return 0;
        }
        char* endp = nullptr;
        const long value = std::strtol(m_cur.text.c_str(), &endp, 10);
        if (!endp || *endp) {
            error("Invalid integer literal '" + m_cur.text + "'");
            return 0;
        }
        advance();
        return static_cast<int>(value);
    }

    bool parseSubtype(VhdlPort& port) {
        if (m_cur.kind != VhdlToken::ID) {
            error("Expected a subtype, got '" + tokenText() + "'");
            return false;
        }
        const std::string type = m_cur.text;
        if (type == "std_logic" || type == "std_ulogic") {
            port.kwd = VBasicDTypeKwd::LOGIC;
            port.isVector = false;
            advance();
            return true;
        }
        if (type == "bit") {
            port.kwd = VBasicDTypeKwd::BIT;
            port.isVector = false;
            advance();
            return true;
        }
        if (type == "boolean") {
            // VHDL boolean is a two-value enumeration; represented as 1-bit logic
            port.kwd = VBasicDTypeKwd::LOGIC;
            port.isVector = false;
            advance();
            return true;
        }
        if (type == "integer" || type == "natural" || type == "positive") {
            port.kwd = VBasicDTypeKwd::INT;
            port.isVector = false;
            advance();
            return true;
        }
        if (type == "std_logic_vector" || type == "unsigned" || type == "signed") {
            port.kwd = VBasicDTypeKwd::LOGIC;
            port.isVector = true;
            port.signing = type == "signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
            advance();
            if (!acceptSym('(')) {
                error("Expected '(' after '" + type + "'");
                return false;
            }
            port.left = expectNumber();
            if (m_failed) return false;
            if (acceptId("downto")) {
                port.ascending = false;
            } else if (acceptId("to")) {
                port.ascending = true;
            } else {
                error("Expected 'downto' or 'to' in range");
                return false;
            }
            port.right = expectNumber();
            if (m_failed) return false;
            if (!acceptSym(')')) {
                error("Expected ')' after range");
                return false;
            }
            return true;
        }
        error("Unsupported subtype '" + type + "'");
        return false;
    }

    void parsePortClause(VhdlEntity& entity) {
        if (!acceptSym('(')) {
            error("Expected '(' after 'port'");
            return;
        }
        for (;;) {
            if (m_failed) return;
            std::vector<std::pair<std::string, FileLine*>> names;
            for (;;) {
                if (m_cur.kind != VhdlToken::ID) {
                    error("Expected a port identifier, got '" + tokenText() + "'");
                    return;
                }
                names.emplace_back(m_cur.text, m_cur.flp);
                advance();
                if (!acceptSym(',')) break;
            }
            if (!acceptSym(':')) {
                error("Expected ':' in port declaration");
                return;
            }
            VDirection direction = VDirection::INPUT;  // VHDL default
            if (acceptId("in")) {
                direction = VDirection::INPUT;
            } else if (acceptId("out")) {
                direction = VDirection::OUTPUT;
            } else if (acceptId("inout")) {
                direction = VDirection::INOUT;
            } else if (acceptId("buffer")) {
                // VHDL buffer is a readable output; read access is not modelled in WP0
                direction = VDirection::OUTPUT;
            }

            VhdlPort base;
            base.dir = direction;
            if (!parseSubtype(base)) return;
            for (const auto& nameFl : names) {
                VhdlPort port = base;
                port.name = nameFl.first;
                port.flp = nameFl.second;
                entity.ports.push_back(std::move(port));
            }

            if (acceptSym(';')) continue;
            if (acceptSym(')')) break;
            error("Expected ';' or ')' in port clause, got '" + tokenText() + "'");
            return;
        }
        if (!acceptSym(';')) error("Expected ';' after port clause");
    }

    void parseEntity(FileLine* entityFlp) {
        const std::string name = expectId();
        if (m_failed) return;
        if (!acceptId("is")) {
            error("Expected 'is' after entity name '" + name + "'");
            return;
        }

        VhdlEntity entity;
        entity.name = name;
        entity.flp = entityFlp;
        entity.libname = m_impp->currentLibname();
        entity.inLibrary = m_impp->currentInLibrary();

        for (;;) {
            if (m_failed) return;
            if (isId("port")) {
                advance();
                parsePortClause(entity);
                continue;
            }
            if (isId("end")) break;
            error("Unsupported declaration in entity '" + name + "': '" + tokenText() + "'");
            return;
        }

        if (!acceptId("end")) {
            error("Expected 'end' for entity '" + name + "'");
            return;
        }
        (void)acceptId("entity");
        if (m_cur.kind == VhdlToken::ID) {
            const std::string endName = m_cur.text;
            advance();
            if (endName != name) {
                errorAt(entityFlp,
                        "Entity end name '" + endName + "' does not match '" + name + "'");
                return;
            }
        }
        if (!acceptSym(';')) {
            error("Expected ';' after 'end' of entity '" + name + "'");
            return;
        }
        m_impp->addEntity(std::move(entity));
    }

    void parseArchitecture(FileLine* archFlp) {
        const std::string name = expectId();
        if (m_failed) return;
        if (!acceptId("of")) {
            error("Expected 'of' after architecture name '" + name + "'");
            return;
        }
        const std::string entityName = expectId();
        if (m_failed) return;
        if (!acceptId("is")) {
            error("Expected 'is' after architecture header");
            return;
        }
        // WP0 supports only an empty declarative part and an empty statement part
        if (!isId("begin")) {
            error("Architecture declarative items are not supported yet");
            return;
        }
        advance();  // 'begin'
        if (!isId("end")) {
            error("Architecture statements are not supported yet");
            return;
        }
        advance();  // 'end'
        (void)acceptId("architecture");
        if (m_cur.kind == VhdlToken::ID) {
            const std::string endName = m_cur.text;
            advance();
            if (endName != name) {
                errorAt(archFlp,
                        "Architecture end name '" + endName + "' does not match '" + name + "'");
                return;
            }
        }
        if (!acceptSym(';')) {
            error("Expected ';' after 'end' of architecture '" + name + "'");
            return;
        }

        VhdlArchitecture arch;
        arch.name = name;
        arch.entityName = entityName;
        arch.flp = archFlp;
        arch.libname = m_impp->currentLibname();
        m_impp->addArchitecture(std::move(arch));
    }

    // Context clauses carry no information for WP0; skip to the terminating semicolon
    void skipContextClause() {
        while (m_cur.kind != VhdlToken::END) {
            if (acceptSym(';')) return;
            advance();
        }
        error("Unexpected end of file in context clause");
    }

public:
    VhdlParser(V3VhdlParseImp* impp, const std::string& filename, std::string text)
        : m_impp{impp}
        , m_lexer{filename, std::move(text)} {
        advance();
    }
    ~VhdlParser() = default;

    void parseDesignFile() {
        while (!m_failed && m_cur.kind != VhdlToken::END) {
            if (isId("library") || isId("use") || isId("context")) {
                skipContextClause();
                continue;
            }
            if (isId("entity")) {
                FileLine* const flp = m_cur.flp;
                advance();
                parseEntity(flp);
                continue;
            }
            if (isId("architecture")) {
                FileLine* const flp = m_cur.flp;
                advance();
                parseArchitecture(flp);
                continue;
            }
            if (isId("package") || isId("configuration") || isId("component")) {
                error("VHDL design unit '" + m_cur.text + "' is not supported yet");
                return;
            }
            error("Unexpected token '" + tokenText() + "' at design-unit level");
            return;
        }
    }
};

}  // namespace

//######################################################################
// V3VhdlParse

V3VhdlParse::V3VhdlParse(AstNetlist* rootp)
    : m_impp{new V3VhdlParseImp{rootp}} {}

V3VhdlParse::~V3VhdlParse() { delete m_impp; }

void V3VhdlParse::parseFile(FileLine* fileline, const std::string& filename, bool inLibrary,
                            bool inLibMap, const std::string& libname, const std::string& errmsg,
                            const std::string& notFoundName) {
    const std::string modname = V3Os::filenameNonDirExt(filename);
    if (v3Global.opt.preprocOnly()) return;  // VHDL has no preprocessor
    if (inLibMap) {
        fileline->v3error("Library map files are not supported for VHDL sources");
        return;
    }

    const std::string resolved = v3Global.opt.filePath(fileline, filename, "", errmsg);
    if (resolved.empty()) {
        if (errmsg != "") return;  // Error already reported
        AstNodeModule* const nodep = new AstNotFoundModule{
            fileline, notFoundName.empty() ? modname : notFoundName, libname};
        m_impp->m_rootp->addModulesp(nodep);
        return;
    }

    for (const VhdlFile& vfile : m_impp->m_files) {
        if (vfile.filename == resolved) return;  // Already collected
    }
    m_impp->m_libname = libname;
    m_impp->m_inLibrary = inLibrary;

    UINFO(3, "Collecting VHDL file " << resolved);
    m_impp->m_files.push_back(VhdlFile{resolved, libname, inLibrary});
}

void V3VhdlParse::finish() { m_impp->finishXml(); }
