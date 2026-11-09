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
#include <climits>
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

    // LCOV_EXCL_START
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
    // LCOV_EXCL_STOP
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
    AstNodeDType* dtp = nullptr;  // Explicit dtype (enums) when set
    bool isVector = false;
    bool isInteger = false;
    VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
    VSigning signing = VSigning::UNSIGNED;
    int left = 0;
    int right = 0;
    bool ascending = false;
    bool isUnconstrained = false;  // Unconstrained array (formal parameter or port)
    int elemWidth = 1;  // Bits per element; >1 for an array of builtin vectors
};

//! How a lowered signal assignment is emitted
enum class AssignMode : uint8_t { CONT, DELAYED, BLOCKING };

class VhdlXmlFrontend final {
    using Node = V3VhdlXmlNode;

    AstNetlist* const m_rootp;  // Netlist to emit into
    const Node* const m_docp;  // Parsed GHDL XML root
    const std::vector<VhdlFile>& m_files;  // Files handed to GHDL
    std::map<std::string, const Node*> m_byId;  // XML nodes indexed by `id`
    std::vector<const Node*> m_entityNodes;  // User entity declarations
    std::vector<const Node*> m_archNodes;  // User architecture bodies
    std::map<std::string, AstVar*> m_scope;  // Current scope: folded name -> Var
    std::map<std::string, AstVar*> m_moduleScope;  // Module declarative part: folded name -> Var
    std::map<std::string, int64_t> m_constBindings;  // Loop parameters during unrolling
    std::map<std::string, const Node*> m_typeBindings;  // Generic type name -> actual type mark
    std::map<std::string, const Node*> m_components;  // Current module: folded name -> component
    AstModule* m_modp = nullptr;  // Module being emitted
    int m_varSeq = 0;  // Sequence for process/generate-local names
    int m_genSeq = 0;  // Sequence for disambiguating sibling generate blocks
    std::string m_genSuffix;  // Suffix for names created inside a generate iteration

    //! Emitted module and its ports, used to bind instances across the hierarchy
    struct ModuleInfo final {
        AstModule* modp = nullptr;  // Emitted module
        std::vector<AstVar*> portList;  // Ports in declaration order
        std::map<std::string, AstVar*> ports;  // Folded port name -> Var
    };
    std::map<std::string, ModuleInfo> m_modules;  // Folded entity name -> base module

    //! One generic of an entity, with its default value expression
    struct GenericDecl final {
        std::string name;
        const Node* defaultp = nullptr;
    };
    std::map<std::string, const Node*> m_entityByName;  // Folded name -> entity
    std::map<std::string, const Node*> m_archByEntityNodes;  // Folded name -> architecture
    std::map<std::string, std::vector<GenericDecl>> m_entityGenerics;  // Folded name -> generics
    std::map<std::string, ModuleInfo> m_specializations;  // Binding key -> specialized module
    std::set<std::string> m_instantiatedEntities;  // Folded names of entities used by instances
    mutable std::map<const Node*, AstNodeDType*> m_enumDtypes;  // Enum type definition -> dtype
    struct RecordMember final {
        std::string name;  // Element name as declared
        AstNodeDType* dtypep = nullptr;  // Element dtype
        int lsb = 0;  // Bit offset in the packed record
        int width = 0;
    };
    struct RecordInfo final {
        AstNodeDType* dtypep = nullptr;  // Packed vector dtype of the record
        std::vector<RecordMember> members;  // Declaration order, most significant first
    };
    struct RecordMemberSpec final {
        std::string name;  // Element name as declared
        const Node* subtypep = nullptr;  // Element subtype, possibly constrained
    };
    mutable std::map<const Node*, RecordInfo> m_records;  // Record type/subtype -> layout
    std::map<AstVar*, RecordInfo> m_varRecords;  // Specialized record ports -> layout
    //! A specialized port's type, and record layout when the port is a record
    struct PortInfo final {
        VhdlDTypeSpec spec;
        RecordInfo record;  // Valid when record.dtypep is set
    };
    mutable bool m_quietErrors = false;  // Type probes must not report diagnostics
    //! One name bound during subprogram inlining: an expression value, or an lvalue
    struct InlineBinding final {
        AstNodeExpr* exprp = nullptr;  // Bound expression
        bool lvalue = false;  // exprp denotes a writable location (out/inout parameter)
        bool readable = true;  // False for an out signal parameter, whose reads are rejected
    };
    using FuncEnv = std::map<std::string, InlineBinding>;  // Subprogram bindings during inlining
    FuncEnv* m_inlineEnvp = nullptr;  // Active subprogram environment, or nullptr
    std::set<const Node*> m_inlineBodies;  // Bodies being inlined (recursion guard)

    //! One active subprogram inlining frame, for lexical capture by nested subprograms
    struct InlineFrame final {
        const Node* bodyp = nullptr;  // Body being inlined
        std::map<std::string, AstVar*> entryScope;  // Caller scope when the frame was entered
        std::map<std::string, AstVar*> liveScope;  // Entry scope plus this frame's locals
        FuncEnv* envp = nullptr;  // This frame's environment (valid while active)
    };
    std::vector<InlineFrame> m_inlineFrames;  // Active frames, outermost first

    //! Pushes an inlining frame and pops it on scope exit
    class InlineFrameGuard final {
        VhdlXmlFrontend& m_frontend;

    public:
        InlineFrameGuard(VhdlXmlFrontend& frontend, const Node* bodyp)
            : m_frontend{frontend} {
            InlineFrame frame;
            frame.bodyp = bodyp;
            frame.entryScope = frontend.m_scope;
            frontend.m_inlineFrames.push_back(std::move(frame));
        }
        ~InlineFrameGuard() { m_frontend.m_inlineFrames.pop_back(); }
        InlineFrameGuard(const InlineFrameGuard&) = delete;
        InlineFrameGuard& operator=(const InlineFrameGuard&) = delete;
    };
    int m_specDepth = 0;  // Generic specialization recursion depth

    // ---- XML access

    FileLine* flpOf(const Node* nodep) const {
        const std::string& file = nodep->attr("file");
        FileLine* const flp = new FileLine{file.empty() ? "<ghdl>" : file};
        const std::string& line = nodep->attr("line");
        if (!line.empty()) flp->lineno(std::atoi(line.c_str()));
        return flp;
    }
    void error(const Node* nodep, const std::string& message) const {
        if (m_quietErrors) return;
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
        if (kind == "entity_declaration" && isUserFile(nodep)
            && !nodep->attr("identifier").empty()) {
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

    //! Build (once) the Verilator enum dtype for an enumeration type definition
    AstNodeDType* enumDType(const Node* typeDefp) const {
        const auto found = m_enumDtypes.find(typeDefp);
        if (found != m_enumDtypes.end()) return found->second;
        FileLine* const flp = flpOf(typeDefp);
        const std::vector<const Node*> lits
            = childEls(typeDefp->child("enumeration_literal_list"));
        int width = 1;
        while ((1 << width) < static_cast<int>(lits.size())) ++width;
        AstBasicDType* const basep = m_rootp->typeTablep()->findLogicBitDType(
            flp, VBasicDTypeKwd::LOGIC, width, width, VSigning::UNSIGNED);
        AstEnumItem* itemsp = nullptr;
        for (size_t i = 0; i < lits.size(); ++i) {
            AstEnumItem* const itemp = new AstEnumItem{
                flp, lits[i]->attr("identifier"), nullptr,
                new AstConst{flp, V3Number{flp, width, static_cast<uint32_t>(i)}}};
            itemsp = itemsp ? AstNode::addNext<AstEnumItem, AstEnumItem>(itemsp, itemp) : itemp;
        }
        // The constructor takes the basis type as a child (op1), which is only legal for
        // parser dtypes not yet in the type table.  Give it a throw-away basis, then
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
        m_enumDtypes[typeDefp] = enump;
        return enump;
    }

    AstNodeDType* enumDTypeOfLiteral(const Node* litp) {
        const Node* const typerefp = litp->child("type");
        if (!typerefp || !typerefp->has("ref")) return nullptr;
        const Node* const defp = nodeById(typerefp->attr("ref"));
        if (!defp || defp->attr("kind") != "enumeration_type_definition") return nullptr;
        return enumDType(defp);
    }

    //! Type definition of the requested kind behind a type mark, or nullptr
    const Node* typeDefOfKind(const Node* nodep, const char* wantKind) const {
        for (int depth = 0; nodep && depth < 8; ++depth) {
            const std::string kind = nodep->attr("kind");
            if (kind == wantKind) return nodep;
            const Node* nextp = nullptr;
            if (kind == "simple_name") {
                const Node* const refp = nodep->child("named_entity");
                if (!refp || !refp->has("ref")) return nullptr;
                nextp = nodeById(refp->attr("ref"));
            } else if (const Node* const defp = nodep->child("type_definition")) {
                nextp = defp;
            } else if (const Node* const typep = nodep->child("type")) {
                nextp = typep->has("ref") ? nodeById(typep->attr("ref")) : typep;
            }
            if (!nextp) return nullptr;
            nodep = nextp;
        }
        return nullptr;
    }

    //! Follow a type/subtype name to its analyzed type node
    const Node* typeNodeOfName(const Node* namep) const {
        if (!namep) return nullptr;
        const Node* const refp = namep->child("named_entity");
        const Node* const declp = refp && refp->has("ref") ? nodeById(refp->attr("ref")) : nullptr;
        if (!declp) return nullptr;
        const std::string kind = declp->attr("kind");
        if (kind == "subtype_declaration") {
            return derefNode(declp->child("subtype_indication"));
        }
        if (kind == "type_declaration" || kind == "anonymous_type_declaration") {
            if (const Node* const subp = declp->child("subtype_definition")) {
                const Node* const typep = derefNode(subp);
                if (typep) return typep;
            }
            return derefNode(declp->child("type_definition"));
        }
        return nullptr;
    }

    //! Record member declarations of a type/subtype, with element constraints applied
    bool recordMemberSpecs(const Node* typep, std::vector<RecordMemberSpec>& members,
                           int depth = 0) const {
        if (!typep || depth > 16) return false;
        const Node* const cur = derefNode(typep);
        if (!cur) return false;
        const std::string kind = cur->attr("kind");
        if (kind == "record_type_definition") {
            members.clear();
            for (const Node* const elemp : childEls(cur->child("elements_declaration_list"))) {
                RecordMemberSpec spec;
                spec.name = elemp->attr("identifier");
                spec.subtypep = elemp->child("subtype_indication");
                if (!spec.subtypep) return false;
                members.push_back(spec);
            }
            return !members.empty();
        }
        if (kind == "record_subtype_definition") {
            const Node* parentp = derefNode(cur->child("parent_type"));
            if (!parentp) parentp = typeNodeOfName(cur->child("subtype_type_mark"));
            if (!parentp || !recordMemberSpecs(parentp, members, depth + 1)) return false;
            // Element constraints in this subtype replace the parent's member subtypes
            for (const Node* const consp : childEls(cur->child("owned_elements_chain"))) {
                if (consp->attr("kind") != "record_element_constraint") continue;
                const std::string name = vhdlFold(consp->attr("identifier"));
                const Node* const subtypep = consp->child("subtype_indication");
                if (!subtypep) continue;
                for (RecordMemberSpec& member : members) {
                    if (vhdlFold(member.name) == name) member.subtypep = subtypep;
                }
            }
            return true;
        }
        if (kind == "simple_name") {
            const Node* const typep2 = typeNodeOfName(cur);
            if (!typep2 || typep2 == cur) return false;
            return recordMemberSpecs(typep2, members, depth + 1);
        }
        return false;
    }

    //! Packed layout of a record type/subtype; false when a member has no width yet
    bool recordLayout(const Node* typep, RecordInfo& info, bool* unconstrainedp = nullptr,
                      int depth = 0) const {
        if (unconstrainedp) *unconstrainedp = false;
        const Node* const cur = derefNode(typep);
        if (!cur) return false;
        // Member ranges can depend on generics or generate parameters; a node-keyed cache
        // is only valid outside any binding context
        const bool cacheable = m_constBindings.empty() && m_typeBindings.empty();
        if (cacheable) {
            const auto found = m_records.find(cur);
            if (found != m_records.end()) {
                info = found->second;
                return true;
            }
        }
        std::vector<RecordMemberSpec> members;
        if (!recordMemberSpecs(cur, members, depth)) return false;
        FileLine* const flp = flpOf(cur);
        info = RecordInfo{};
        int width = 0;
        bool fourstate = false;
        for (const RecordMemberSpec& spec : members) {
            const VhdlDTypeSpec probe = probeTypeSpec(spec.subtypep);
            if (probe.isUnconstrained) {
                if (unconstrainedp) *unconstrainedp = true;
                return false;
            }
            const VhdlDTypeSpec memberSpec = parseType(spec.subtypep);
            if (memberSpec.isUnconstrained) {
                if (unconstrainedp) *unconstrainedp = true;
                return false;
            }
            AstNodeDType* const dtypep = dtypeOf(memberSpec, flpOf(spec.subtypep));
            if (!dtypep || dtypep->width() <= 0) return false;
            RecordMember member;
            member.name = spec.name;
            member.dtypep = dtypep;
            member.width = dtypep->width();
            width += member.width;
            fourstate |= dtypep->isFourstate();
            info.members.push_back(member);
        }
        if (info.members.empty() || width <= 0) return false;
        // The first declared element occupies the most significant bits
        int lsb = width;
        for (RecordMember& member : info.members) {
            lsb -= member.width;
            member.lsb = lsb;
        }
        info.dtypep = m_rootp->typeTablep()->findLogicBitDType(
            flp, fourstate ? VBasicDTypeKwd::LOGIC : VBasicDTypeKwd::BIT, width, width,
            VSigning::UNSIGNED);
        if (cacheable) m_records[cur] = info;
        return true;
    }

    //! Record layout of an expression/name's analyzed type
    bool recordLayoutOfName(const Node* nodep, RecordInfo& info) const {
        if (!nodep) return false;
        const Node* const typep = nodep->child("type");
        const Node* const defp
            = typep && typep->has("ref") ? nodeById(typep->attr("ref")) : nullptr;
        if (defp && recordLayout(defp, info)) return true;
        // An unconstrained record port specialized from its instance actual carries the
        // layout only on the specialized port variable
        if (nodep->attr("kind") == "simple_name") {
            if (AstVar* const varp = lookupVar(nodep)) {
                const auto found = m_varRecords.find(varp);
                if (found != m_varRecords.end()) {
                    info = found->second;
                    return true;
                }
            }
            if (m_inlineEnvp) {
                const auto it = m_inlineEnvp->find(vhdlFold(nodep->attr("identifier")));
                if (it != m_inlineEnvp->end()) {
                    if (AstNodeVarRef* const refp = VN_CAST(it->second.exprp, NodeVarRef)) {
                        const auto found = m_varRecords.find(refp->varp());
                        if (found != m_varRecords.end()) {
                            info = found->second;
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    //! Declarations resolved at their use sites, or not modeled by Verilator
    static bool ignorableDeclaration(const std::string& kind) {
        return kind == "constant_declaration" || kind == "type_declaration"
               || kind == "anonymous_type_declaration" || kind == "subtype_declaration"
               || kind == "function_declaration" || kind == "procedure_declaration"
               || kind == "function_body" || kind == "procedure_body"
               || kind == "attribute_declaration" || kind == "attribute_specification"
               || kind == "alias_declaration" || kind == "object_alias_declaration"
               || kind == "non_object_alias_declaration" || kind == "file_declaration"
               || kind == "use_clause";
    }

    //! Follow a pure reference node to its target
    const Node* derefNode(const Node* nodep) const {
        for (int depth = 0; nodep && depth < 16 && !nodep->has("kind") && nodep->has("ref");
             ++depth) {
            nodep = nodeById(nodep->attr("ref"));
        }
        return nodep;
    }

    //! Number of indices in a VHDL range; 0 for a null range
    static int rangeLength(int left, int right, bool ascending) {
        const int length = ascending ? right - left + 1 : left - right + 1;
        return length > 0 ? length : 0;
    }

    //! Width of one array element subtype, or 0 when it cannot be determined
    int elementWidth(const Node* elemp, int depth = 0) const {
        if (!elemp || depth > 16) return 0;
        if (!elemp->has("kind") && elemp->has("ref")) {
            return elementWidth(nodeById(elemp->attr("ref")), depth + 1);
        }
        const std::string kind = elemp->attr("kind");
        if (kind == "enumeration_type_definition" || kind == "enumeration_subtype_definition") {
            return 1;
        }
        if (kind == "integer_subtype_definition") {
            return m_rootp->typeTablep()
                ->findBasicDType(flpOf(elemp), VBasicDTypeKwd::INT)
                ->width();
        }
        if (kind == "array_subtype_definition") {
            const std::vector<const Node*> indices
                = childEls(elemp->child("index_constraint_list"));
            const Node* rangep
                = indices.size() == 1 ? indices.front()->child("range_constraint") : nullptr;
            rangep = derefNode(rangep);
            int64_t leftValue = 0;
            int64_t rightValue = 0;
            if (!rangep || !evalConst(rangeBound(rangep, true), leftValue)
                || !evalConst(rangeBound(rangep, false), rightValue)) {
                return 0;
            }
            const bool ascending = rangep->attr("direction") == "to";
            const int elems = rangeLength(static_cast<int>(leftValue),
                                          static_cast<int>(rightValue), ascending);
            const Node* elemsubp = elemp->child("element_subtype");
            if (!elemsubp) elemsubp = elemp->child("element_subtype_indication");
            const int elemWidth = elementWidth(elemsubp, depth + 1);
            if (elems <= 0 || elemWidth <= 0) return 0;
            return elems * elemWidth;
        }
        if (kind == "simple_name") {
            const std::string id = vhdlFold(elemp->attr("identifier"));
            if (id == "std_logic" || id == "std_ulogic" || id == "bit" || id == "boolean") {
                return 1;
            }
            if (id == "integer" || id == "natural" || id == "positive") {
                return m_rootp->typeTablep()
                    ->findBasicDType(flpOf(elemp), VBasicDTypeKwd::INT)
                    ->width();
            }
            const Node* const refp = elemp->child("named_entity");
            const Node* const declp
                = refp && refp->has("ref") ? nodeById(refp->attr("ref")) : nullptr;
            if (!declp) return 0;
            const std::string declKind = declp->attr("kind");
            if (declKind == "subtype_declaration") {
                return elementWidth(declp->child("subtype_indication"), depth + 1);
            }
            if (declKind == "type_declaration" || declKind == "anonymous_type_declaration") {
                if (const Node* const defp = declp->child("type_definition")) {
                    return elementWidth(defp, depth + 1);
                }
                return elementWidth(declp->child("subtype_definition"), depth + 1);
            }
            return 0;
        }
        if (kind == "record_type_definition" || kind == "record_subtype_definition") {
            RecordInfo info;
            return recordLayout(elemp, info) ? info.dtypep->width() : 0;
        }
        return 0;
    }

    //! Analyzed array layout of a name or expression, when its type is constrained
    bool arraySpec(const Node* nodep, VhdlDTypeSpec& spec) const {
        const Node* typep = nodep ? nodep->child("type") : nullptr;
        if (!typep || !typep->has("ref")) return false;
        const Node* defp = nodeById(typep->attr("ref"));
        if (!defp) return false;
        if (!defp->has("kind") && defp->has("ref")) defp = nodeById(defp->attr("ref"));
        if (!defp) return false;
        const std::string kind = defp->attr("kind");
        if (kind == "array_subtype_definition") {
            if (childEls(defp->child("index_constraint_list")).empty()) return false;
        } else if (kind != "simple_name" && kind != "subtype_declaration"
                   && kind != "type_declaration" && kind != "anonymous_type_declaration") {
            return false;
        }
        const VhdlDTypeSpec parsed = probeTypeSpec(defp);
        if (!parsed.isVector) return false;
        spec = parsed;
        return true;
    }

    //! Resolve a type without reporting diagnostics
    VhdlDTypeSpec probeTypeSpec(const Node* subtypep) const {
        // A probe must not repeat diagnostics the declaration already reported
        const bool savedQuiet = m_quietErrors;
        m_quietErrors = true;
        const VhdlDTypeSpec spec = parseType(subtypep);
        m_quietErrors = savedQuiet;
        return spec;
    }

    //! Dtype of a subprogram formal; nullptr when the formal is unconstrained
    AstNodeDType* formalDType(const Node* paramp) {
        const VhdlDTypeSpec probe = probeTypeSpec(paramp->child("subtype_indication"));
        if (probe.isUnconstrained) return nullptr;
        return dtypeOf(parseType(paramp->child("subtype_indication")), flpOf(paramp));
    }

    //! Whether a name or expression is an array of multi-bit elements
    bool multiArraySpec(const Node* nodep, VhdlDTypeSpec& spec) const {
        return arraySpec(nodep, spec) && spec.elemWidth > 1;
    }

    VhdlDTypeSpec parseType(const Node* subtypep, int depth = 0) const {
        VhdlDTypeSpec spec;
        if (!subtypep || depth > 16) return spec;
        if (!subtypep->has("kind") && subtypep->has("ref")) {
            return parseType(nodeById(subtypep->attr("ref")), depth + 1);
        }
        const std::string kind = subtypep->attr("kind");
        if (kind == "integer_subtype_definition") {
            spec.isInteger = true;
            spec.kwd = VBasicDTypeKwd::INT;
            return spec;
        }
        if (kind == "enumeration_subtype_definition") {
            return parseType(subtypep->child("subtype_type_mark"), depth + 1);
        }
        if (kind == "simple_name") {
            const std::string id = vhdlFold(subtypep->attr("identifier"));
            if (m_typeBindings.find(id) != m_typeBindings.end()) {
                // Generic type parameter: resolve through the instance's binding
                return parseType(m_typeBindings.find(id)->second, depth + 1);
            }
            if (id == "std_logic" || id == "std_ulogic" || id == "boolean") {
                spec.kwd = VBasicDTypeKwd::LOGIC;
            } else if (id == "bit") {
                spec.kwd = VBasicDTypeKwd::BIT;
            } else if (id == "integer" || id == "natural" || id == "positive") {
                spec.isInteger = true;
                spec.kwd = VBasicDTypeKwd::INT;
            } else {
                // User-defined subtype or type: follow the analyzed declaration
                const Node* const refp = subtypep->child("named_entity");
                if (refp && refp->has("ref")) {
                    const Node* const declp = nodeById(refp->attr("ref"));
                    if (declp) {
                        const std::string declKind = declp->attr("kind");
                        if (declKind == "subtype_declaration") {
                            if (const Node* const subp = declp->child("subtype_indication")) {
                                return parseType(subp, depth + 1);
                            }
                        } else if (declKind == "type_declaration"
                                   || declKind == "anonymous_type_declaration") {
                            const Node* subp = declp->child("subtype_definition");
                            if (subp && !subp->has("kind") && subp->has("ref")) {
                                subp = nodeById(subp->attr("ref"));
                            }
                            if (subp) {
                                // A type_definition reached through a subtype_definition
                                const std::string subKind = subp->attr("kind");
                                if (subKind == "record_type_definition"
                                    || subKind == "record_subtype_definition") {
                                    return parseType(subp, depth + 1);
                                }
                                if (subKind == "enumeration_type_definition") {
                                    spec.dtp = enumDType(subp);
                                    return spec;
                                }
                                return parseType(subp, depth + 1);
                            }
                            if (const Node* const defp = declp->child("type_definition")) {
                                if (defp->attr("kind") == "enumeration_type_definition") {
                                    spec.dtp = enumDType(defp);
                                    return spec;
                                }
                                if (defp->attr("kind") == "record_type_definition"
                                    || defp->attr("kind") == "record_subtype_definition") {
                                    return parseType(defp, depth + 1);
                                }
                            }
                        }
                    }
                }
                error(subtypep,
                      "VHDL type '" + subtypep->attr("identifier") + "' is not supported yet");
            }
            return spec;
        }
        if (kind == "record_type_definition" || kind == "record_subtype_definition") {
            RecordInfo info;
            bool unconstrained = false;
            if (recordLayout(subtypep, info, &unconstrained) && info.dtypep) {
                spec.dtp = info.dtypep;
            } else if (unconstrained) {
                spec.isUnconstrained = true;
            } else {
                error(subtypep, "VHDL record type is not supported yet");
            }
            return spec;
        }
        if (kind != "array_subtype_definition") {
            error(subtypep, "VHDL type form '" + kind + "' is not supported yet");
            return spec;
        }

        const Node* elemSubp = subtypep->child("element_subtype");
        if (!elemSubp) elemSubp = subtypep->child("element_subtype_indication");
        const Node* const markp = subtypep->child("subtype_type_mark");
        std::string mark = markp ? vhdlFold(markp->attr("identifier")) : std::string{};
        if (mark.empty()) {
            // Anonymous array type (type X is array (...) of T): derive from the element type
            const Node* elemp = nullptr;
            if (elemSubp) {
                elemp = elemSubp->has("ref") ? nodeById(elemSubp->attr("ref")) : elemSubp;
            }
            if (elemp) {
                if (const Node* const elemMarkp = elemp->child("subtype_type_mark")) {
                    mark = vhdlFold(elemMarkp->attr("identifier"));
                } else if (elemp->attr("kind") == "simple_name") {
                    mark = vhdlFold(elemp->attr("identifier"));
                } else if (elemp->attr("kind") == "enumeration_type_definition") {
                    // Only '0'/'1' literals is a bit-like element; otherwise std_logic-like
                    const auto lits = childEls(elemp->child("enumeration_literal_list"));
                    mark = lits.size() == 2 ? "bit" : "std_logic";
                }
            }
        }
        // The array is modeled as one flat vector with elemWidth bits per element
        const VhdlDTypeSpec elemSpec = probeTypeSpec(elemSubp);
        int elemWidth = 0;
        if (elemSpec.dtp) {
            elemWidth = elemSpec.dtp->width();  // Enum/record element width
        } else if (elemSpec.isVector) {
            elemWidth = dtypeOf(elemSpec, flpOf(subtypep))->width();  // Nested array bits
        }
        if (elemWidth <= 0) elemWidth = elementWidth(elemSubp);
        if (elemWidth <= 0) {
            unsupported(subtypep, "array element type");
            return spec;
        }
        if (mark == "signed") {
            spec.kwd = VBasicDTypeKwd::LOGIC;
            spec.signing = VSigning::SIGNED;
        } else if (mark == "unsigned" || mark == "std_logic_vector" || mark == "std_ulogic_vector"
                   || mark == "std_logic" || mark == "std_ulogic") {
            spec.kwd = VBasicDTypeKwd::LOGIC;
            spec.signing = VSigning::UNSIGNED;
        } else if (mark == "bit_vector" || mark == "bit") {
            spec.kwd = VBasicDTypeKwd::BIT;
        } else {
            // A named element type (subtype, record, enumeration, ...): use its analyzed type
            const AstBasicDType* elemBasicp = nullptr;
            if (elemSpec.dtp) elemBasicp = VN_CAST(elemSpec.dtp->skipRefp(), BasicDType);
            if (elemSpec.isVector) {
                spec.kwd = elemSpec.kwd;
                spec.signing = elemSpec.signing;
            } else if (elemBasicp) {
                spec.kwd = elemBasicp->keyword();
                spec.signing = elemBasicp->numeric();
            } else if (!elemSpec.isInteger) {
                // A named scalar subtype of bit or std_logic
                spec.kwd = elemSpec.kwd;
                spec.signing = elemSpec.signing;
            } else {
                error(subtypep, "VHDL array type '" + mark + "' is not supported yet");
                return spec;
            }
        }
        spec.elemWidth = elemWidth;
        spec.isVector = true;

        const Node* const indicesp = subtypep->child("index_constraint_list");
        if (!indicesp) {
            error(subtypep, "unconstrained VHDL array is not supported yet");
            spec.isVector = false;  // Keep the tree buildable; the error aborts the run
            spec.isUnconstrained = true;
            return spec;
        }
        const std::vector<const Node*> indices = childEls(indicesp);
        if (indices.size() != 1) {
            error(subtypep, "multi-dimensional VHDL arrays are not supported yet");
            spec.isVector = false;
            return spec;
        }
        const Node* const rangep = derefNode(indices.front()->child("range_constraint"));
        if (!rangep) {
            error(subtypep, "VHDL array without a range constraint");
            spec.isVector = false;
            return spec;
        }
        spec.ascending = rangep->attr("direction") == "to";
        int64_t leftValue = 0;
        int64_t rightValue = 0;
        if (!evalConst(rangeBound(rangep, true), leftValue)
            || !evalConst(rangeBound(rangep, false), rightValue)) {
            error(rangep, "non-static VHDL range limit is not supported yet");
            spec.isVector = false;  // Keep the tree buildable; the error aborts the run
            return spec;
        }
        spec.left = static_cast<int>(leftValue);
        spec.right = static_cast<int>(rightValue);
        if (rangeLength(spec.left, spec.right, spec.ascending) <= 0) {
            unsupported(subtypep, "null array");
            spec.isVector = false;  // Keep the tree buildable; the error aborts the run
        }
        return spec;
    }

    AstNodeDType* dtypeOf(const VhdlDTypeSpec& spec, FileLine* flp) const {
        if (spec.dtp) return spec.dtp;
        AstTypeTable* const tablep = m_rootp->typeTablep();
        if (spec.isInteger) return tablep->findBasicDType(flp, VBasicDTypeKwd::INT);
        if (!spec.isVector) return tablep->findBasicDType(flp, spec.kwd);
        const int elems = rangeLength(spec.left, spec.right, spec.ascending);
        // An array of vectors keeps only its total bit width; the VHDL index range and
        // element width are recovered from the analyzed type where needed
        if (spec.elemWidth > 1) {
            const int width = elems * spec.elemWidth;
            return tablep->findLogicBitDType(flp, spec.kwd, width, width, spec.signing);
        }
        return tablep->findLogicBitDType(
            flp, spec.kwd, VNumRange{spec.left, spec.right, spec.ascending}, elems, spec.signing);
    }

    //! Dtype of a node's analyzed result type (its <type ref>), if any
    AstNodeDType* typeRefDType(const Node* nodep) {
        const Node* const typep = nodep->child("type");
        if (!typep || !typep->has("ref")) return nullptr;
        const Node* const defp = nodeById(typep->attr("ref"));
        if (!defp) return nullptr;
        const std::string kind = defp->attr("kind");
        if (kind == "enumeration_type_definition") return enumDType(defp);
        if (kind == "record_type_definition" || kind == "record_subtype_definition") {
            return probeTypeSpec(defp).dtp;
        }
        if (kind == "array_subtype_definition"
            && childEls(defp->child("index_constraint_list")).empty()) {
            return nullptr;  // Unconstrained result type: use the expression's type
        }
        return dtypeOf(parseType(defp), flpOf(nodep));
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

    //! Lower a simple, indexed, sliced or selected name; write selects lvalue access
    AstNodeExpr* lowerName(const Node* nodep, bool write) {
        const std::string kind = nodep->attr("kind");
        if (kind == "selected_element") {
            // Record member: a bit slice of the packed record vector
            AstNodeExpr* const basep = lowerName(nodep->child("prefix"), write);
            RecordInfo info;
            if (!recordLayoutOfName(nodep->child("prefix"), info)) {
                error(nodep, "VHDL selection is not a record member");
                return basep;
            }
            const std::string name = vhdlFold(nodep->attr("identifier"));
            for (const RecordMember& member : info.members) {
                if (vhdlFold(member.name) == name) {
                    AstSel* const selp = new AstSel{flpOf(nodep), basep, member.lsb, member.width};
                    selp->dtypep(member.dtypep);
                    return selp;
                }
            }
            error(nodep, "VHDL record member '" + nodep->attr("identifier") + "' not found");
            return basep;
        }
        if (kind == "indexed_name" || kind == "slice_name") {
            const Node* const prefixp = nodep->child("prefix");
            if (!prefixp) {
                unsupported(nodep, "name form '" + kind + "'");
                return makeConst(flpOf(nodep), bitDType(flpOf(nodep)), 0);
            }
            AstNodeExpr* const basep = lowerName(prefixp, write);
            return lowerVectorName(nodep, kind, basep);
        }

        const Node* const namep = nodep;
        if (const Node* const refp = namep->child("named_entity")) {
            if (refp->has("ref")) {
                const Node* const declp = nodeById(refp->attr("ref"));
                if (declp && declp->attr("kind") == "object_alias_declaration") {
                    return lowerName(declp->child("name"), write);
                }
            }
        }
        if (m_inlineEnvp) {
            const auto it = m_inlineEnvp->find(vhdlFold(namep->attr("identifier")));
            if (it != m_inlineEnvp->end()) {
                if (write && !it->second.lvalue) {
                    error(nodep, "VHDL input parameter or constant cannot be assigned");
                    return makeConst(flpOf(nodep), bitDType(flpOf(nodep)), 0);
                }
                if (!write && !it->second.readable) {
                    unsupported(nodep, "read of an out procedure parameter");
                    return makeConst(flpOf(nodep), bitDType(flpOf(nodep)), 0);
                }
                AstNodeExpr* const valuep = it->second.exprp->cloneTree(false);
                if (!write) setReadAccess(valuep);
                return valuep;
            }
        }
        const auto bindIt = m_constBindings.find(vhdlFold(namep->attr("identifier")));
        if (bindIt != m_constBindings.end()) {
            if (write) {
                error(nodep, "VHDL loop parameter cannot be assigned");
                return makeConst(flpOf(nodep), bitDType(flpOf(nodep)), 0);
            }
            return makeConst(flpOf(nodep), intDType(flpOf(nodep)),
                             static_cast<uint32_t>(bindIt->second));
        }
        AstVar* const varp = lookupVar(namep);
        if (!varp) {
            // Indexing/slicing a constant resolves through its declared value
            if (const Node* const refp = namep->child("named_entity")) {
                if (refp->has("ref")) {
                    const Node* const declp = nodeById(refp->attr("ref"));
                    if (declp) {
                        const std::string declKind = declp->attr("kind");
                        if (declKind == "constant_declaration"
                            || declKind == "interface_constant_declaration") {
                            if (write) {
                                error(nodep, "VHDL constant cannot be assigned");
                                return makeConst(flpOf(nodep), bitDType(flpOf(nodep)), 0);
                            }
                            if (const Node* const defp = declp->child("default_value")) {
                                return lowerExpr(defp, nullptr);
                            }
                        }
                    }
                }
            }
            error(namep, "Unknown VHDL identifier '" + namep->attr("identifier") + "'");
            return makeConst(flpOf(nodep), bitDType(flpOf(nodep)), 0);
        }
        return new AstVarRef{flpOf(namep), varp, write ? VAccess::WRITE : VAccess::READ};
    }

    //! Lower an index or slice of an already-lowered vector expression
    AstNodeExpr* lowerVectorName(const Node* nodep, const std::string& kind,
                                 AstNodeExpr* const basep) {
        FileLine* const flp = flpOf(nodep);
        const AstBasicDType* const basicp = VN_CAST(basep->dtypep()->skipRefp(), BasicDType);
        if (!basicp || basicp->width() <= 1) {
            error(nodep, "Indexing a non-vector VHDL name is not supported yet");
            return basep;
        }
        // An array of multi-bit elements only carries its total bit width in the lowered
        // dtype; the VHDL index range and element width come from the analyzed type
        if (kind == "indexed_name") {
            const std::vector<const Node*> indices = childEls(nodep->child("index_list"));
            if (indices.size() != 1) {
                unsupported(nodep, "multi-dimensional index");
                return basep;
            }
            VhdlDTypeSpec layout;
            const bool haveLayout = arraySpec(nodep->child("prefix"), layout);
            const bool multi = haveLayout && layout.elemWidth > 1;
            const int elemWidth = multi ? layout.elemWidth : 1;
            const int totalWidth = basicp->width();
            // The rightmost element is the least significant one
            const bool ascending = haveLayout ? layout.ascending : basicp->ascending();
            const int arrayRight = haveLayout ? layout.right : basicp->right();
            const auto setSelDType = [&](AstSel* selp) {
                selp->dtypep(m_rootp->typeTablep()->findLogicBitDType(flp, layout.kwd, elemWidth,
                                                                      elemWidth, layout.signing));
            };
            if (indices.front()->attr("kind") == "integer_literal") {
                const int index = static_cast<int>(
                    std::strtol(indices.front()->attr("value").c_str(), nullptr, 10));
                const int offset
                    = (ascending ? arrayRight - index : index - arrayRight) * elemWidth;
                if (offset < 0 || offset + elemWidth > totalWidth) {
                    error(nodep, "VHDL index is out of range");
                    return basep;
                }
                AstSel* const selp = new AstSel{flp, basep, offset, elemWidth};
                if (multi) setSelDType(selp);
                return selp;
            }
            AstNodeExpr* const indexExprp = lowerExpr(indices.front(), intDType(flp));
            if (!indexExprp) return basep;
            AstNodeExpr* offsetp = nullptr;
            if (ascending) {
                offsetp = new AstSub{flp, makeConst(flp, intDType(flp), arrayRight), indexExprp};
            } else {
                offsetp = new AstSub{flp, indexExprp, makeConst(flp, intDType(flp), arrayRight)};
            }
            offsetp->dtypep(intDType(flp));
            if (multi) {
                offsetp = new AstMul{flp, offsetp, makeConst(flp, intDType(flp), elemWidth)};
                offsetp->dtypep(intDType(flp));
            }
            AstSel* const selp = new AstSel{flp, basep, offsetp, elemWidth};
            if (multi) setSelDType(selp);
            return selp;
        }

        // slice_name; only one-dimensional arrays can be sliced
        const Node* const suffixp = derefNode(nodep->child("suffix"));
        if (!suffixp || suffixp->attr("kind") != "range_expression") {
            unsupported(nodep, "slice form");
            return basep;
        }
        // An array of multi-bit elements keeps one element width per slice element
        VhdlDTypeSpec layout;
        const bool haveLayout = arraySpec(nodep->child("prefix"), layout);
        const bool multi = haveLayout && layout.elemWidth > 1;
        const bool ascending = suffixp->attr("direction") == "to";
        const bool arrayAscending = haveLayout ? layout.ascending : basicp->ascending();
        if (ascending != arrayAscending) {
            error(nodep, "VHDL slice direction does not match the array");
            return basep;
        }
        const int arrayRight = haveLayout ? layout.right : basicp->right();
        const int elemWidth = multi ? layout.elemWidth : 1;
        int64_t leftValue = 0;
        int64_t rightValue = 0;
        const bool staticBounds = evalConst(rangeBound(suffixp, true), leftValue)
                                  && evalConst(rangeBound(suffixp, false), rightValue);
        if (!staticBounds) {
            // The position may vary, but the width must be known to type the result
            AstNodeExpr* const leftExprp = lowerExpr(rangeBound(suffixp, true), intDType(flp));
            AstNodeExpr* const rightExprp = lowerExpr(rangeBound(suffixp, false), intDType(flp));
            if (!leftExprp || !rightExprp) return basep;
            AstNodeExpr* const lowExprp = ascending ? leftExprp : rightExprp;
            AstNodeExpr* const highExprp = ascending ? rightExprp : leftExprp;
            int64_t elems = 0;
            if (!staticRangeWidth(highExprp, lowExprp, elems)) {
                unsupported(nodep, "non-static slice bounds");
                return basep;
            }
            if (elems <= 0) {
                unsupported(nodep, "null slice");
                return basep;
            }
            if (elems > INT_MAX / elemWidth) {
                error(nodep, "VHDL slice bound is out of range");
                return basep;
            }
            // The slice's right bound is its least significant element
            AstNodeExpr* offsetp = nullptr;
            if (ascending) {
                offsetp = new AstSub{flp, makeConst(flp, intDType(flp), arrayRight), rightExprp};
            } else {
                offsetp = new AstSub{flp, rightExprp, makeConst(flp, intDType(flp), arrayRight)};
            }
            offsetp->dtypep(intDType(flp));
            if (elemWidth > 1) {
                offsetp = new AstMul{flp, offsetp, makeConst(flp, intDType(flp), elemWidth)};
                offsetp->dtypep(intDType(flp));
            }
            const int width = static_cast<int>(elems) * elemWidth;
            AstSel* const selp = new AstSel{flp, basep, offsetp, width};
            if (multi) {
                selp->dtypep(m_rootp->typeTablep()->findLogicBitDType(flp, layout.kwd, width,
                                                                      width, layout.signing));
            } else {
                selp->dtypep(m_rootp->typeTablep()->findLogicBitDType(
                    flp, basicp->keyword(), width, width, basicp->numeric()));
            }
            return selp;
        }
        if (leftValue < INT_MIN || leftValue > INT_MAX || rightValue < INT_MIN
            || rightValue > INT_MAX) {
            error(nodep, "VHDL slice bound is out of range");
            return basep;
        }
        const int left = static_cast<int>(leftValue);
        const int right = static_cast<int>(rightValue);
        const int offset = (ascending ? arrayRight - right : right - arrayRight) * elemWidth;
        const int elems = rangeLength(left, right, ascending);
        if (elems <= 0) {
            unsupported(nodep, "null slice");
            return basep;
        }
        const int width = elems * elemWidth;
        if (offset < 0 || offset + width > basicp->width()) {
            error(nodep, "VHDL slice is out of range");
            return basep;
        }
        AstSel* const selp = new AstSel{flp, basep, offset, width};
        if (multi) {
            VhdlDTypeSpec sliceSpec = layout;
            sliceSpec.left = left;
            sliceSpec.right = right;
            sliceSpec.ascending = ascending;
            selp->dtypep(dtypeOf(sliceSpec, flp));
        } else {
            // AstSel defaults to unsigned; a slice of a signed array stays signed
            selp->dtypep(m_rootp->typeTablep()->findLogicBitDType(flp, basicp->keyword(), width,
                                                                  width, basicp->numeric()));
        }
        return selp;
    }

    AstNodeExpr* lowerLValue(const Node* stmtp, const Node* targetp) {
        if (!targetp) {
            unsupported(stmtp, "assignment without a target");
            return nullptr;
        }
        const std::string kind = targetp->attr("kind");
        if (kind == "simple_name" || kind == "indexed_name" || kind == "slice_name"
            || kind == "selected_element") {
            return lowerName(targetp, true);
        }
        unsupported(targetp, "assignment target form");
        return nullptr;
    }

    //! Resize/reinterpret an expression to a target dtype using post-width nodes.
    //! AstCast cannot be used: VHDL mode skips the width pass that lowers casts.
    AstNodeExpr* resizeExpr(AstNodeExpr* fromp, AstNodeDType* toDtype) {
        FileLine* const flp = fromp->fileline();
        AstBasicDType* const toBasicp = VN_CAST(toDtype->skipRefp(), BasicDType);
        AstBasicDType* const fromBasicp = VN_CAST(fromp->dtypep()->skipRefp(), BasicDType);
        if (!toBasicp || !fromBasicp || toBasicp->width() <= 0 || fromBasicp->width() <= 0) {
            flp->v3error(
                "VHDL conversion between non-basic types is not supported yet");  // LCOV_EXCL_LINE
            return fromp;
        }
        const int toWidth = toBasicp->width();
        const int fromWidth = fromBasicp->width();
        AstNodeExpr* resultp = fromp;
        if (toWidth > fromWidth) {
            resultp = fromBasicp->isSigned()
                          ? static_cast<AstNodeExpr*>(new AstExtendS{flp, fromp, toWidth})
                          : static_cast<AstNodeExpr*>(new AstExtend{flp, fromp, toWidth});
        } else if (toWidth < fromWidth) {
            resultp = new AstSel{flp, fromp, 0, toWidth};
        }
        resultp->dtypep(toDtype);
        return resultp;
    }

    //! Lower the numeric_std conversion builtins needed by synthesizable VHDL
    AstNodeExpr* lowerCall(const Node* nodep, AstNodeDType* contextp) {
        FileLine* const flp = flpOf(nodep);
        const Node* const prefixp = nodep->child("prefix");
        const std::string fn = prefixp ? vhdlFold(prefixp->attr("identifier")) : std::string{};
        const std::vector<const Node*> argsp
            = childEls(nodep->child("parameter_association_chain"));
        const auto actualOf = [&](size_t index) -> const Node* {
            if (index >= argsp.size()) return nullptr;
            return argsp[index]->child("actual");
        };

        if (fn == "to_unsigned" || fn == "to_signed" || fn == "resize") {
            const Node* const valuep = actualOf(0);
            const Node* const widthp = actualOf(1);
            int64_t widthValue = 0;
            if (!valuep || !widthp || !evalConst(widthp, widthValue) || widthValue <= 0
                || widthValue > INT_MAX) {
                unsupported(nodep, "non-static width in " + fn);
                return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
            }
            const int width = static_cast<int>(widthValue);
            AstNodeExpr* const valueExprp = lowerExpr(valuep, nullptr);
            if (!valueExprp) return nullptr;

            VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
            VSigning signing = fn == "to_signed" ? VSigning::SIGNED : VSigning::UNSIGNED;
            if (fn == "resize") {
                const AstBasicDType* const srcp
                    = VN_CAST(valueExprp->dtypep()->skipRefp(), BasicDType);
                if (!srcp || srcp->width() <= 1) {
                    unsupported(nodep, "resize of a non-vector value");
                    return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
                }
                kwd = srcp->keyword();
                signing = srcp->numeric();
            }
            AstNodeDType* const dtp
                = m_rootp->typeTablep()->findLogicBitDType(flp, kwd, width, width, signing);
            return resizeExpr(valueExprp, dtp);
        }
        if (fn == "shift_left" || fn == "shift_right") {
            const Node* const valuep = actualOf(0);
            const Node* const amountp = actualOf(1);
            AstNodeExpr* const valueExprp = valuep ? lowerExpr(valuep, nullptr) : nullptr;
            AstNodeExpr* const amountExprp = amountp ? lowerExpr(amountp, nullptr) : nullptr;
            if (!valueExprp || !amountExprp) {
                unsupported(nodep, fn + " without two arguments");
                return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
            }
            AstNodeDType* const dtp = valueExprp->dtypep();
            AstNodeExpr* resultp = nullptr;
            // numeric_std shift_right sign-extends signed operands, shift_left does not
            if (fn == "shift_left") {
                resultp = new AstShiftL{flp, valueExprp, amountExprp, dtp->width()};
            } else if (dtp->isSigned()) {
                resultp = new AstShiftRS{flp, valueExprp, amountExprp, dtp->width()};
            } else {
                resultp = new AstShiftR{flp, valueExprp, amountExprp, dtp->width()};
            }
            resultp->dtypep(dtp);
            return resultp;
        }
        if (fn == "rotate_left" || fn == "rotate_right") {
            const Node* const valuep = actualOf(0);
            const Node* const amountp = actualOf(1);
            AstNodeExpr* const valueExprp = valuep ? lowerExpr(valuep, nullptr) : nullptr;
            AstNodeExpr* const amountExprp = amountp ? lowerExpr(amountp, nullptr) : nullptr;
            if (!valueExprp || !amountExprp) {
                unsupported(nodep, fn + " without two arguments");
                return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
            }
            AstNodeDType* const dtp = valueExprp->dtypep();
            const int width = dtp->width();
            // Counts at or beyond the width wrap, so rotate by count mod width
            AstNodeExpr* const nmodp
                = new AstModDiv{flp, amountExprp, makeConst(flp, intDType(flp), width)};
            nmodp->dtypep(intDType(flp));
            AstNodeExpr* const restp
                = new AstSub{flp, makeConst(flp, intDType(flp), width), nmodp->cloneTree(false)};
            restp->dtypep(intDType(flp));
            AstNodeExpr* const innerShiftp
                = fn == "rotate_left"
                      ? static_cast<AstNodeExpr*>(
                            new AstShiftL{flp, valueExprp, nmodp->cloneTree(false), width})
                      : static_cast<AstNodeExpr*>(
                            new AstShiftR{flp, valueExprp, nmodp->cloneTree(false), width});
            AstNodeExpr* const outerShiftp
                = fn == "rotate_left"
                      ? static_cast<AstNodeExpr*>(
                            new AstShiftR{flp, valueExprp->cloneTree(false), restp, width})
                      : static_cast<AstNodeExpr*>(
                            new AstShiftL{flp, valueExprp->cloneTree(false), restp, width});
            innerShiftp->dtypep(dtp);
            outerShiftp->dtypep(dtp);
            AstNodeExpr* const resultp = new AstOr{flp, innerShiftp, outerShiftp};
            resultp->dtypep(dtp);
            return resultp;
        }
        if (fn == "minimum" || fn == "maximum") {
            const Node* const lhsp = actualOf(0);
            const Node* const rhsp = actualOf(1);
            AstNodeExpr* const lhsExprp = lhsp ? lowerExpr(lhsp, nullptr) : nullptr;
            AstNodeExpr* const rhsExprp
                = rhsp ? lowerExpr(rhsp, lhsExprp ? lhsExprp->dtypep() : nullptr) : nullptr;
            if (!lhsExprp || !rhsExprp) {
                unsupported(nodep, fn + " without two arguments");
                return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
            }
            AstNodeDType* const dtp = lhsExprp->dtypep();
            const bool signedp = dtp->isSigned();
            AstNodeExpr* cmp;
            if (fn == "minimum") {
                cmp = signedp ? static_cast<AstNodeExpr*>(new AstLtS{flp, lhsExprp, rhsExprp})
                              : static_cast<AstNodeExpr*>(new AstLt{flp, lhsExprp, rhsExprp});
            } else {
                cmp = signedp ? static_cast<AstNodeExpr*>(new AstGtS{flp, lhsExprp, rhsExprp})
                              : static_cast<AstNodeExpr*>(new AstGt{flp, lhsExprp, rhsExprp});
            }
            cmp->dtypep(bitDType(flp));
            AstCond* const resultp
                = new AstCond{flp, cmp, lhsExprp->cloneTree(false), rhsExprp->cloneTree(false)};
            resultp->dtypep(dtp);
            return resultp;
        }
        if (fn == "to_integer" || fn == "conv_integer") {
            const Node* const valuep = actualOf(0);
            if (!valuep) {
                unsupported(nodep, fn + " without an argument");
                return makeConst(flp, intDType(flp), 0);
            }
            AstNodeExpr* const valueExprp = lowerExpr(valuep, nullptr);
            if (!valueExprp) return nullptr;
            return resizeExpr(valueExprp, intDType(flp));
        }

        // User-defined functions: inline the analyzed body
        if (functionDeclOf(nodep)) {
            if (AstNodeExpr* const inlinedp = inlineFunction(nodep)) return inlinedp;
        }
        unsupported(nodep, "function call '" + fn + "'");
        return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
    }

    // ---- User-defined function inlining

    static void deleteFuncEnv(FuncEnv& env) {
        for (auto& pair : env) {
            AstNode* const nodep = pair.second.exprp;
            if (nodep) VL_DO_DANGLING(nodep->deleteTree(), nodep);
        }
        env.clear();
    }

    static FuncEnv cloneFuncEnv(const FuncEnv& env) {
        FuncEnv out;
        for (const auto& pair : env) {
            out[pair.first] = InlineBinding{pair.second.exprp->cloneTree(false),
                                            pair.second.lvalue, pair.second.readable};
        }
        return out;
    }

    static void funcEnvSet(FuncEnv& env, const std::string& name, AstNodeExpr* nodep,
                           bool lvalue = false, bool readable = true) {
        const auto it = env.find(name);
        if (it == env.end()) {
            env[name] = InlineBinding{nodep, lvalue, readable};
            return;
        }
        AstNode* const oldp = it->second.exprp;
        it->second = InlineBinding{nodep, lvalue, readable};
        if (oldp) VL_DO_DANGLING(oldp->deleteTree(), oldp);
    }

    //! Mark every variable reference as a read; used when an out/inout lvalue is read back
    static void setReadAccess(AstNodeExpr* exprp) {
        exprp->foreach([](AstVarRef* refp) { refp->access(VAccess::READ); });
    }

    //! Merge variable values of two branch environments into env under condp
    void funcEnvMerge(FuncEnv& env, AstNodeExpr* const condp, const FuncEnv& thenEnv,
                      const FuncEnv& elseEnv) {
        std::set<std::string> names;
        for (const auto& pair : thenEnv) names.insert(pair.first);
        for (const auto& pair : elseEnv) names.insert(pair.first);
        for (const auto& name : names) {
            const auto thenIt = thenEnv.find(name);
            const auto elseIt = elseEnv.find(name);
            AstNodeExpr* thenp
                = thenIt != thenEnv.end() ? thenIt->second.exprp->cloneTree(false) : nullptr;
            AstNodeExpr* elsep
                = elseIt != elseEnv.end() ? elseIt->second.exprp->cloneTree(false) : nullptr;
            if (!thenp) thenp = elsep->cloneTree(false);
            if (!elsep) elsep = thenp->cloneTree(false);
            AstCond* const condp2
                = new AstCond{condp->fileline(), condp->cloneTree(false), thenp, elsep};
            condp2->dtypep(thenp->dtypep());
            funcEnvSet(env, name, condp2);
        }
    }

    //! Evaluate a function-body statement list; returns the function result of this path,
    //! or nullptr when the path falls off the end of the list.  Updates env.
    AstNodeExpr* evalFuncBlock(const std::vector<const Node*>& stmts, size_t index, FuncEnv& env,
                               const Node* bodyp, int depth) {
        VL_RESTORER(m_inlineEnvp);
        m_inlineEnvp = &env;
        if (depth > 64) {
            error(bodyp, "VHDL function statement nesting is too deep");
            return nullptr;
        }
        for (size_t i = index; i < stmts.size(); ++i) {
            const Node* const stmtp = stmts[i];
            const std::string kind = stmtp->attr("kind");
            if (kind == "variable_assignment_statement") {
                const Node* const targetp = stmtp->child("target");
                const Node* const exprp = stmtp->child("expression");
                if (!targetp || !exprp) {
                    unsupported(stmtp, "variable assignment without a target");
                    return nullptr;
                }
                if (targetp->attr("kind") != "simple_name") {
                    // Function locals are bound to values, so sub-element updates cannot
                    // be applied without a deep value rewrite
                    unsupported(stmtp, "assignment to a variable element in an inlined function");
                    return nullptr;
                }
                AstNodeExpr* const valuep = lowerExpr(exprp, nullptr);
                if (!valuep) return nullptr;
                funcEnvSet(env, vhdlFold(targetp->attr("identifier")), valuep);
                continue;
            }
            if (kind == "null_statement" || kind == "assertion_statement"
                || kind == "report_statement") {
                continue;
            }
            if (kind == "return_statement") {
                return lowerExpr(stmtp->child("expression"), nullptr);
            }
            if (kind == "if_statement") {
                return evalFuncIf(stmtp, stmts, i, env, bodyp, depth + 1);
            }
            if (kind == "case_statement") {
                return evalFuncCase(stmtp, stmts, i, env, bodyp, depth + 1);
            }
            unsupported(stmtp, "statement '" + kind + "' in a VHDL function body");
            return nullptr;
        }
        return nullptr;
    }

    //! Evaluate an if/elsif/else statement plus the statements that follow it
    AstNodeExpr* evalFuncIf(const Node* ifp, const std::vector<const Node*>& stmts, size_t index,
                            FuncEnv& env, const Node* bodyp, int depth) {
        VL_RESTORER(m_inlineEnvp);
        m_inlineEnvp = &env;
        AstNodeExpr* const condp = lowerExpr(ifp->child("condition"), bitDType(flpOf(ifp)));
        if (!condp) return nullptr;
        FuncEnv thenEnv = cloneFuncEnv(env);
        AstNodeExpr* thenResultp = evalFuncBlock(
            childEls(ifp->child("sequential_statement_chain")), 0, thenEnv, bodyp, depth);
        FuncEnv elseEnv = cloneFuncEnv(env);
        AstNodeExpr* elseResultp
            = evalFuncElse(ifp->child("else_clause"), stmts, index + 1, elseEnv, bodyp, depth);
        if (!thenResultp) { thenResultp = evalFuncBlock(stmts, index + 1, thenEnv, bodyp, depth); }
        if (!elseResultp) { elseResultp = evalFuncBlock(stmts, index + 1, elseEnv, bodyp, depth); }
        if (!thenResultp || !elseResultp) {
            error(ifp, "VHDL function does not return a value on all paths");
            if (thenResultp) VL_DO_DANGLING(thenResultp->deleteTree(), thenResultp);
            if (elseResultp) VL_DO_DANGLING(elseResultp->deleteTree(), elseResultp);
            deleteFuncEnv(thenEnv);
            deleteFuncEnv(elseEnv);
            return nullptr;
        }
        AstCond* const resultp = new AstCond{flpOf(ifp), condp, thenResultp, elseResultp};
        resultp->dtypep(thenResultp->dtypep());
        deleteFuncEnv(thenEnv);
        deleteFuncEnv(elseEnv);
        return resultp;
    }

    //! Evaluate an else/elsif clause chain, continuing with stmts[index] when it falls through
    AstNodeExpr* evalFuncElse(const Node* elsep, const std::vector<const Node*>& stmts,
                              size_t index, FuncEnv& env, const Node* bodyp, int depth) {
        VL_RESTORER(m_inlineEnvp);
        m_inlineEnvp = &env;
        if (!elsep) return evalFuncBlock(stmts, index, env, bodyp, depth);
        const std::string kind = elsep->attr("kind");
        const Node* const condp = elsep->child("condition");
        if (kind == "else" || !condp) {
            AstNodeExpr* resultp = evalFuncBlock(
                childEls(elsep->child("sequential_statement_chain")), 0, env, bodyp, depth);
            if (!resultp) resultp = evalFuncBlock(stmts, index, env, bodyp, depth);
            return resultp;
        }
        AstNodeExpr* const condExprp = lowerExpr(condp, bitDType(flpOf(elsep)));
        if (!condExprp) return nullptr;
        FuncEnv thenEnv = cloneFuncEnv(env);
        AstNodeExpr* thenResultp = evalFuncBlock(
            childEls(elsep->child("sequential_statement_chain")), 0, thenEnv, bodyp, depth);
        FuncEnv elseEnv = cloneFuncEnv(env);
        AstNodeExpr* elseResultp
            = evalFuncElse(elsep->child("else_clause"), stmts, index, elseEnv, bodyp, depth);
        if (!thenResultp) thenResultp = evalFuncBlock(stmts, index, thenEnv, bodyp, depth);
        if (!elseResultp) elseResultp = evalFuncBlock(stmts, index, elseEnv, bodyp, depth);
        if (!thenResultp || !elseResultp) {
            error(elsep, "VHDL function does not return a value on all paths");
            if (thenResultp) VL_DO_DANGLING(thenResultp->deleteTree(), thenResultp);
            if (elseResultp) VL_DO_DANGLING(elseResultp->deleteTree(), elseResultp);
            deleteFuncEnv(thenEnv);
            deleteFuncEnv(elseEnv);
            return nullptr;
        }
        AstCond* const resultp = new AstCond{flpOf(elsep), condExprp, thenResultp, elseResultp};
        resultp->dtypep(thenResultp->dtypep());
        deleteFuncEnv(thenEnv);
        deleteFuncEnv(elseEnv);
        return resultp;
    }

    //! Evaluate a case statement plus the statements that follow it
    AstNodeExpr* evalFuncCase(const Node* casep, const std::vector<const Node*>& stmts,
                              size_t index, FuncEnv& env, const Node* bodyp, int depth) {
        VL_RESTORER(m_inlineEnvp);
        m_inlineEnvp = &env;
        AstNodeExpr* const selectorp = lowerExpr(casep->child("expression"), nullptr);
        if (!selectorp) return nullptr;
        struct CaseBranch final {
            AstNodeExpr* condp = nullptr;  // nullptr for the others choice
            AstNodeExpr* resultp = nullptr;
            FuncEnv env;
        };
        std::vector<CaseBranch> branches;
        size_t othersIndex = 0;
        bool haveOthers = false;
        bool allFellThrough = true;
        for (const Node* const altp : childEls(casep->child("case_statement_alternative_chain"))) {
            CaseBranch branch;
            branch.env = cloneFuncEnv(env);
            branch.resultp = evalFuncBlock(childEls(altp->child("associated_chain")), 0,
                                           branch.env, bodyp, depth);
            if (!branch.resultp) {
                branch.resultp = evalFuncBlock(stmts, index + 1, branch.env, bodyp, depth);
            }
            allFellThrough = allFellThrough && !branch.resultp;
            if (altp->attr("kind") == "choice_by_others") {
                haveOthers = true;
                othersIndex = branches.size();
            } else if (const Node* const choicep = altp->child("choice_expression")) {
                AstNodeExpr* const choiceValp = lowerExpr(choicep, selectorp->dtypep());
                if (!choiceValp) {
                    deleteFuncEnv(branch.env);
                    if (branch.resultp)
                        VL_DO_DANGLING(branch.resultp->deleteTree(), branch.resultp);
                    for (CaseBranch& other : branches) {
                        if (other.resultp) {
                            VL_DO_DANGLING(other.resultp->deleteTree(), other.resultp);
                        }
                        if (other.condp) VL_DO_DANGLING(other.condp->deleteTree(), other.condp);
                        deleteFuncEnv(other.env);
                    }
                    return nullptr;
                }
                AstNodeExpr* const eqp
                    = new AstEq{flpOf(altp), selectorp->cloneTree(false), choiceValp};
                eqp->dtypep(bitDType(flpOf(altp)));
                branch.condp = eqp;
            } else {
                unsupported(altp, "case choice form");
                deleteFuncEnv(branch.env);
                if (branch.resultp) VL_DO_DANGLING(branch.resultp->deleteTree(), branch.resultp);
                for (CaseBranch& other : branches) {
                    if (other.resultp) VL_DO_DANGLING(other.resultp->deleteTree(), other.resultp);
                    if (other.condp) VL_DO_DANGLING(other.condp->deleteTree(), other.condp);
                    deleteFuncEnv(other.env);
                }
                return nullptr;
            }
            branches.push_back(branch);
        }
        if (allFellThrough) {
            // Every branch continues after the case: merge the environments
            const size_t baseIndex = haveOthers ? othersIndex : branches.size() - 1;
            FuncEnv merged = cloneFuncEnv(branches[baseIndex].env);
            for (size_t i = 0; i < branches.size(); ++i) {
                if (i == baseIndex || !branches[i].condp) continue;
                funcEnvMerge(merged, branches[i].condp, branches[i].env, merged);
            }
            for (CaseBranch& branch : branches) {
                if (branch.condp) VL_DO_DANGLING(branch.condp->deleteTree(), branch.condp);
                deleteFuncEnv(branch.env);
            }
            deleteFuncEnv(env);
            env = std::move(merged);
            VL_DO_DANGLING(selectorp->deleteTree(), selectorp);
            return nullptr;
        }
        if (!haveOthers) { error(casep, "VHDL case in a function does not cover all choices"); }
        AstNodeExpr* resultp = nullptr;
        if (haveOthers) {
            resultp = branches[othersIndex].resultp;
            branches[othersIndex].resultp = nullptr;
            if (!resultp) {
                error(casep, "VHDL function does not return a value on all paths");
                deleteFuncEnv(branches[othersIndex].env);
                for (CaseBranch& branch : branches) {
                    if (branch.resultp)
                        VL_DO_DANGLING(branch.resultp->deleteTree(), branch.resultp);
                    if (branch.condp) VL_DO_DANGLING(branch.condp->deleteTree(), branch.condp);
                    deleteFuncEnv(branch.env);
                }
                VL_DO_DANGLING(selectorp->deleteTree(), selectorp);
                return nullptr;
            }
        }
        for (size_t i = branches.size(); i-- > 0;) {
            CaseBranch& branch = branches[i];
            if (!branch.condp) continue;
            if (!branch.resultp || !resultp) {
                error(casep, "VHDL function does not return a value on all paths");
                if (branch.resultp) VL_DO_DANGLING(branch.resultp->deleteTree(), branch.resultp);
                if (resultp) VL_DO_DANGLING(resultp->deleteTree(), resultp);
                for (CaseBranch& other : branches) {
                    if (other.condp) VL_DO_DANGLING(other.condp->deleteTree(), other.condp);
                    deleteFuncEnv(other.env);
                }
                VL_DO_DANGLING(selectorp->deleteTree(), selectorp);
                return nullptr;
            }
            AstCond* const condp
                = new AstCond{flpOf(casep), branch.condp, branch.resultp, resultp};
            condp->dtypep(branch.resultp->dtypep());
            branch.condp = nullptr;
            resultp = condp;
        }
        for (CaseBranch& branch : branches) {
            if (branch.condp) VL_DO_DANGLING(branch.condp->deleteTree(), branch.condp);
            deleteFuncEnv(branch.env);
        }
        VL_DO_DANGLING(selectorp->deleteTree(), selectorp);
        return resultp;
    }

    //! Whether function-body statements can be lowered by inlining
    static bool funcStatementsSupported(const Node* chainp, int depth) {
        if (depth > 32) return false;
        for (const Node* const stmtp : childEls(chainp)) {
            const std::string kind = stmtp->attr("kind");
            if (kind == "variable_assignment_statement" || kind == "return_statement"
                || kind == "null_statement" || kind == "assertion_statement"
                || kind == "report_statement") {
                continue;
            }
            if (kind == "if_statement") {
                if (!funcStatementsSupported(stmtp->child("sequential_statement_chain"),
                                             depth + 1)) {
                    return false;
                }
                for (const Node* elsep = stmtp->child("else_clause"); elsep;
                     elsep = elsep->child("else_clause")) {
                    if (!funcStatementsSupported(elsep->child("sequential_statement_chain"),
                                                 depth + 1)) {
                        return false;
                    }
                    if (!elsep->child("condition")) break;
                }
                continue;
            }
            if (kind == "case_statement") {
                for (const Node* const altp :
                     childEls(stmtp->child("case_statement_alternative_chain"))) {
                    if (!funcStatementsSupported(altp->child("associated_chain"), depth + 1)) {
                        return false;
                    }
                }
                continue;
            }
            return false;
        }
        return true;
    }

    //! Find the analyzed declaration of a called function, or nullptr
    const Node* functionDeclOf(const Node* callp) const {
        const Node* const implp = callp->child("implementation");
        if (!implp || !implp->has("ref")) return nullptr;
        const Node* const declp = nodeById(implp->attr("ref"));
        if (!declp || declp->attr("kind") != "function_declaration") return nullptr;
        return declp;
    }

    //! Inline a call to a user-defined pure function; nullptr if it cannot be inlined
    AstNodeExpr* inlineFunction(const Node* nodep) {
        const Node* const declp = functionDeclOf(nodep);
        if (!declp) return nullptr;
        const Node* const bodyrefp = declp->child("subprogram_body");
        if (!bodyrefp) return nullptr;
        const Node* const bodyp
            = bodyrefp->has("ref") ? nodeById(bodyrefp->attr("ref")) : bodyrefp;
        if (!bodyp || bodyp->attr("kind") != "function_body") return nullptr;
        if (!funcStatementsSupported(bodyp->child("sequential_statement_chain"), 0)) {
            return nullptr;
        }
        for (const Node* const declp2 : childEls(bodyp->child("declaration_chain"))) {
            const std::string kind = declp2->attr("kind");
            if (kind != "variable_declaration" && kind != "constant_declaration"
                && !ignorableDeclaration(kind)) {
                return nullptr;
            }
        }
        if (!m_inlineBodies.insert(bodyp).second) return nullptr;  // Recursive call
        AstNodeExpr* const resultp = inlineFunctionBody(nodep, declp, bodyp);
        m_inlineBodies.erase(bodyp);
        return resultp;
    }

    AstNodeExpr* inlineFunctionBody(const Node* callp, const Node* declp, const Node* bodyp) {
        FileLine* const flp = flpOf(callp);
        std::map<std::string, AstVar*> captureScope;
        FuncEnv env;
        captureBaseFor(declp, captureScope, env);
        const std::vector<const Node*> params
            = childEls(declp->child("interface_declaration_chain"));
        std::map<std::string, const Node*> actualByName;
        std::vector<const Node*> positional;
        for (const Node* const assocp : childEls(callp->child("parameter_association_chain"))) {
            const Node* const formalp = assocp->child("formal");
            const Node* const actualp = assocp->child("actual");
            if (formalp) {
                actualByName[vhdlFold(formalp->attr("identifier"))] = actualp;
            } else {
                positional.push_back(actualp);
            }
        }
        size_t positionalIndex = 0;
        for (const Node* const paramp : params) {
            const std::string paramKind = paramp->attr("kind");
            if (paramKind != "interface_constant_declaration"
                && paramKind != "interface_signal_declaration"
                && paramKind != "interface_variable_declaration") {
                unsupported(paramp, "function parameter form");
                deleteFuncEnv(env);
                return nullptr;
            }
            const std::string paramMode = paramp->attr("mode");
            if (!paramMode.empty() && paramMode != "in") {
                unsupported(paramp, "function parameter mode '" + paramMode + "'");
                deleteFuncEnv(env);
                return nullptr;
            }
            const std::string name = vhdlFold(paramp->attr("identifier"));
            const Node* actualp = nullptr;
            const auto namedIt = actualByName.find(name);
            if (namedIt != actualByName.end()) {
                actualp = namedIt->second;
            } else if (positionalIndex < positional.size()) {
                actualp = positional[positionalIndex++];
            }
            if (!actualp) actualp = paramp->child("default_value");
            if (!actualp) {
                error(callp, "VHDL function call is missing argument '"
                                 + paramp->attr("identifier") + "'");
                deleteFuncEnv(env);
                return nullptr;
            }
            AstNodeDType* const dtp = formalDType(paramp);  // nullptr when unconstrained
            AstNodeExpr* const valuep = lowerExpr(actualp, dtp);
            if (!valuep) {
                deleteFuncEnv(env);
                return nullptr;
            }
            funcEnvSet(env, name, valuep);
        }
        VL_RESTORER(m_inlineEnvp);
        VL_RESTORER_COPY(m_scope);
        InlineFrameGuard frameGuard{*this, bodyp};  // Captures the caller's scope
        m_scope = captureScope;
        m_inlineEnvp = &env;
        m_inlineFrames.back().liveScope = m_scope;
        m_inlineFrames.back().envp = &env;
        for (const Node* const declp2 : childEls(bodyp->child("declaration_chain"))) {
            const std::string kind = declp2->attr("kind");
            if (kind != "variable_declaration" && kind != "constant_declaration") continue;
            const std::string name = vhdlFold(declp2->attr("identifier"));
            AstNodeDType* const dtp
                = dtypeOf(parseType(declp2->child("subtype_indication")), flpOf(declp2));
            AstNodeExpr* valuep = nullptr;
            if (const Node* const defp = declp2->child("default_value")) {
                valuep = lowerExpr(defp, dtp);
            }
            if (!valuep) valuep = makeConst(flpOf(declp2), dtp, 0);
            funcEnvSet(env, name, valuep);
        }
        AstNodeExpr* resultp = evalFuncBlock(childEls(bodyp->child("sequential_statement_chain")),
                                             0, env, bodyp, 0);
        if (!resultp) { error(callp, "VHDL function does not return a value on all paths"); }
        deleteFuncEnv(env);
        if (!resultp) return nullptr;
        if (AstNodeDType* const resultDtp = typeRefDType(callp)) {
            return resizeExpr(resultp, resultDtp);
        }
        return resultp;
    }

    // ---- User-defined procedure inlining

    //! Find the analyzed declaration of a called procedure, or nullptr
    const Node* procedureDeclOf(const Node* callp) const {
        const Node* implp = callp->child("implementation");
        if (!implp) {
            const Node* const prefixp = callp->child("prefix");
            implp = prefixp ? prefixp->child("named_entity") : nullptr;
        }
        if (!implp || !implp->has("ref")) return nullptr;
        const Node* const declp = nodeById(implp->attr("ref"));
        if (!declp || declp->attr("kind") != "procedure_declaration") return nullptr;
        return declp;
    }

    //! Find the analyzed body of a subprogram declaration, or nullptr
    const Node* subprogramBodyOf(const Node* declp) const {
        const Node* const bodyrefp = declp->child("subprogram_body");
        if (!bodyrefp) return nullptr;
        return bodyrefp->has("ref") ? nodeById(bodyrefp->attr("ref")) : bodyrefp;
    }

    //! Whether `nodep` is lexically inside `containerp` in the analyzed tree
    bool isLexicallyInside(const Node* nodep, const Node* containerp) const {
        if (!containerp) return false;
        for (int depth = 0; nodep && depth < 64; ++depth) {
            if (nodep == containerp) return true;
            const Node* const parentp = nodep->child("parent");
            if (!parentp || !parentp->has("ref")) return false;
            nodep = nodeById(parentp->attr("ref"));
        }
        return false;
    }

    //! Whether a subprogram is declared in a process or generate declarative part
    bool subprogramIsProcessLocal(const Node* declp) const {
        const Node* nodep = declp;
        for (int depth = 0; nodep && depth < 64; ++depth) {
            const Node* const parentp = nodep->child("parent");
            if (!parentp || !parentp->has("ref")) return false;
            nodep = nodeById(parentp->attr("ref"));
            if (!nodep) return false;
            const std::string kind = nodep->attr("kind");
            if (kind == "process_statement" || kind == "sensitized_process_statement"
                || kind == "generate_statement_body") {
                return true;
            }
            if (kind == "architecture_body" || kind == "package_body"
                || kind == "package_declaration" || kind == "entity_declaration") {
                return false;
            }
        }
        return false;
    }

    //! Scope and environment visible to a subprogram from its lexical position.
    //! Subprograms nested in an active inlining frame capture that frame's own scope and
    //! parameters; siblings and outer-scope subprograms capture the enclosing level.
    void captureBaseFor(const Node* declp, std::map<std::string, AstVar*>& scope,
                        FuncEnv& env) const {
        size_t ancestor = m_inlineFrames.size();
        for (size_t i = m_inlineFrames.size(); i-- > 0;) {
            if (isLexicallyInside(declp, m_inlineFrames[i].bodyp)) {
                ancestor = i;
                break;
            }
        }
        if (ancestor != m_inlineFrames.size()) {
            scope = m_inlineFrames[ancestor].liveScope;
            if (m_inlineFrames[ancestor].envp) {
                env = cloneFuncEnv(*m_inlineFrames[ancestor].envp);
            }
            return;
        }
        if (subprogramIsProcessLocal(declp)) {
            scope = m_inlineFrames.empty() ? m_scope : m_inlineFrames.front().entryScope;
        } else {
            scope = m_moduleScope;
        }
    }

    //! Inline a procedure call.  The body statements replace the call; out/inout
    //! parameters are bound to the actual lvalues.
    AstNode* lowerProcedureCall(const Node* stmtp, AssignMode mode) {
        const Node* const callp = stmtp->child("procedure_call");
        if (!callp) {
            unsupported(stmtp, "procedure call form");
            return nullptr;
        }
        const Node* const declp = procedureDeclOf(callp);
        if (!declp) {
            unsupported(callp, "procedure call target");
            return nullptr;
        }
        const Node* const bodyp = subprogramBodyOf(declp);
        if (!bodyp || bodyp->attr("kind") != "procedure_body") {
            unsupported(callp, "procedure without a body");
            return nullptr;
        }
        if (!m_inlineBodies.insert(bodyp).second) {
            unsupported(callp, "recursive procedure call");
            return nullptr;
        }
        AstNode* const resultp = inlineProcedureBody(callp, declp, bodyp, mode);
        m_inlineBodies.erase(bodyp);
        return resultp;
    }

    AstNode* inlineProcedureBody(const Node* callp, const Node* declp, const Node* bodyp,
                                 AssignMode mode) {
        std::map<std::string, AstVar*> captureScope;
        FuncEnv calleeEnv;
        captureBaseFor(declp, captureScope, calleeEnv);
        AstNode* prologsp = nullptr;  // Out-parameter initializations
        // Match actuals to formals in the caller's scope
        std::map<std::string, const Node*> actualByName;
        std::vector<const Node*> positional;
        if (const Node* const assocsp = callp->child("parameter_association_chain")) {
            for (const Node* const assocp : childEls(assocsp)) {
                if (assocp->attr("kind") != "association_element_by_expression") {
                    unsupported(assocp, "procedure association form");
                    deleteFuncEnv(calleeEnv);
                    return nullptr;
                }
                const Node* const formalp = assocp->child("formal");
                const Node* const actualp = assocp->child("actual");
                if (formalp) {
                    actualByName[vhdlFold(formalp->attr("identifier"))] = actualp;
                } else {
                    positional.push_back(actualp);
                }
            }
        }
        size_t positionalIndex = 0;
        for (const Node* const paramp : childEls(declp->child("interface_declaration_chain"))) {
            const std::string paramKind = paramp->attr("kind");
            if (paramKind != "interface_constant_declaration"
                && paramKind != "interface_signal_declaration"
                && paramKind != "interface_variable_declaration") {
                unsupported(paramp, "procedure parameter form");
                deleteFuncEnv(calleeEnv);
                return nullptr;
            }
            const std::string name = vhdlFold(paramp->attr("identifier"));
            const Node* actualp = nullptr;
            const auto namedIt = actualByName.find(name);
            if (namedIt != actualByName.end()) {
                actualp = namedIt->second;
            } else if (positionalIndex < positional.size()) {
                actualp = positional[positionalIndex++];
            }
            if (!actualp) actualp = paramp->child("default_value");
            if (!actualp) {
                error(callp, "VHDL procedure call is missing argument '"
                                 + paramp->attr("identifier") + "'");
                deleteFuncEnv(calleeEnv);
                return nullptr;
            }
            const std::string paramMode = paramp->attr("mode");
            AstNodeDType* dtp = formalDType(paramp);  // nullptr when unconstrained
            if (paramMode.empty() || paramMode == "in") {
                AstNodeExpr* const valuep = lowerExpr(actualp, dtp);
                if (!valuep) {
                    deleteFuncEnv(calleeEnv);
                    return nullptr;
                }
                funcEnvSet(calleeEnv, name, valuep, false);
            } else if (paramMode == "out" || paramMode == "inout" || paramMode == "buffer") {
                AstNodeExpr* const lhsp = lowerLValue(callp, actualp);
                if (!lhsp) {
                    deleteFuncEnv(calleeEnv);
                    return nullptr;
                }
                const bool isSignal = paramKind == "interface_signal_declaration";
                AstNodeDType* const argDtp = dtp ? dtp : lhsp->dtypep();
                if (paramMode == "out") {
                    // An out parameter starts at its subtype's default value.  Binding it
                    // directly to the actual also performs the copy-out.
                    FileLine* const pflp = flpOf(paramp);
                    AstNodeExpr* const defp = makeConst(pflp, argDtp, 0);
                    AstNodeExpr* const lhspInit = lhsp->cloneTree(false);
                    AstNode* initp = nullptr;
                    if (isSignal && mode == AssignMode::DELAYED) {
                        initp = new AstAssignDly{pflp, lhspInit, defp};
                    } else if (isSignal && mode == AssignMode::CONT) {
                        initp = new AstAssignW{pflp, lhspInit, defp};
                    } else {
                        initp = new AstAssign{pflp, lhspInit, defp};
                    }
                    prologsp
                        = prologsp ? AstNode::addNext<AstNode, AstNode>(prologsp, initp) : initp;
                }
                // Reading an out signal parameter before it is written would see the old
                // actual value rather than the formal's default; reject that case
                funcEnvSet(calleeEnv, name, lhsp, true, paramMode != "out" || !isSignal);
            } else {
                unsupported(paramp, "procedure parameter mode '" + paramMode + "'");
                deleteFuncEnv(calleeEnv);
                return nullptr;
            }
        }

        VL_RESTORER(m_inlineEnvp);
        VL_RESTORER_COPY(m_scope);
        InlineFrameGuard frameGuard{*this, bodyp};  // Captures the caller's scope
        m_scope = captureScope;
        m_inlineEnvp = &calleeEnv;
        m_inlineFrames.back().liveScope = m_scope;
        m_inlineFrames.back().envp = &calleeEnv;

        // Locals are reinitialized on every call, like automatic variables
        AstNode* initsp = nullptr;
        for (const Node* const declp2 : childEls(bodyp->child("declaration_chain"))) {
            const std::string kind = declp2->attr("kind");
            if (kind == "variable_declaration") {
                const Node* const subtypep = declp2->child("subtype_indication");
                if (!subtypep) {
                    error(declp2, "VHDL variable without a subtype");
                    continue;
                }
                FileLine* const dflp = flpOf(declp2);
                const std::string vname = declp2->attr("identifier");
                const std::string mangled
                    = "vhdlproc_" + std::to_string(++m_varSeq) + "__" + vname;
                AstVar* const varp
                    = new AstVar{dflp, VVarType::VAR, mangled, dtypeOf(parseType(subtypep), dflp)};
                m_modp->addStmtsp(varp);
                m_scope[vhdlFold(vname)] = varp;
                m_inlineFrames.back().liveScope = m_scope;
                AstNodeExpr* rhsp = nullptr;
                if (const Node* const defp = declp2->child("default_value")) {
                    rhsp = lowerExpr(defp, varp->dtypep());
                }
                if (!rhsp) rhsp = makeConst(dflp, varp->dtypep(), 0);
                AstVarRef* const lhsp = new AstVarRef{dflp, varp, VAccess::WRITE};
                AstNode* const initp = new AstAssign{dflp, lhsp, rhsp};
                initsp = initsp ? AstNode::addNext<AstNode, AstNode>(initsp, initp) : initp;
            } else if (kind != "constant_declaration" && !ignorableDeclaration(kind)) {
                unsupported(declp2, "procedure declaration '" + kind + "'");
            }
        }

        bool fallsThrough = true;
        AstNode* const bodysp
            = lowerProcChain(bodyp->child("sequential_statement_chain"), mode, fallsThrough);
        AstNode* resultp = prologsp;
        if (initsp)
            resultp = resultp ? AstNode::addNext<AstNode, AstNode>(resultp, initsp) : initsp;
        if (bodysp)
            resultp = resultp ? AstNode::addNext<AstNode, AstNode>(resultp, bodysp) : bodysp;
        deleteFuncEnv(calleeEnv);
        return resultp;
    }

    //! Lower a procedure statement chain.  `fallsThroughp` is false when every path
    //! returns before the end of the chain.
    AstNode* lowerProcChain(const Node* chainp, AssignMode mode, bool& fallsThroughp) {
        return lowerProcChain(childEls(chainp), 0, mode, fallsThroughp);
    }

    AstNode* lowerProcChain(const std::vector<const Node*>& stmts, size_t index, AssignMode mode,
                            bool& fallsThroughp) {
        AstNode* headp = nullptr;
        for (size_t i = index; i < stmts.size(); ++i) {
            const Node* const stmtp = stmts[i];
            const std::string kind = stmtp->attr("kind");
            if (kind == "return_statement") {
                if (stmtp->child("expression")) { unsupported(stmtp, "procedure return value"); }
                fallsThroughp = false;
                return headp;
            }
            if (kind == "if_statement") {
                AstNode* const ifp = lowerProcIf(stmtp, stmts, i + 1, mode, fallsThroughp);
                if (!ifp) return headp;
                return headp ? AstNode::addNext<AstNode, AstNode>(headp, ifp) : ifp;
            }
            if (kind == "case_statement") {
                AstNode* const casep = lowerProcCase(stmtp, stmts, i + 1, mode, fallsThroughp);
                if (!casep) return headp;
                return headp ? AstNode::addNext<AstNode, AstNode>(headp, casep) : casep;
            }
            AstNode* const nodep = lowerStmt(stmtp, mode);
            if (!nodep) continue;
            headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
        }
        fallsThroughp = true;
        return headp;
    }

    //! Lower `stmts[index..]` followed by `tail[tailIndex..]` as one chain.  The tail is
    //! re-lowered per falling branch, so an early return truncates it in that branch only.
    AstNode* lowerProcChainWithTail(const std::vector<const Node*>& stmts, size_t index,
                                    const std::vector<const Node*>& tail, size_t tailIndex,
                                    AssignMode mode, bool& fallsThroughp) {
        std::vector<const Node*> combined;
        combined.reserve(stmts.size() - index + tail.size() - tailIndex);
        combined.insert(combined.end(), stmts.begin() + index, stmts.end());
        combined.insert(combined.end(), tail.begin() + tailIndex, tail.end());
        return lowerProcChain(combined, 0, mode, fallsThroughp);
    }

    //! Lower an if/elsif construct, appending the following statements to each branch
    AstNode* lowerProcIf(const Node* ifp, const std::vector<const Node*>& tail, size_t tailIndex,
                         AssignMode mode, bool& fallsThroughp) {
        FileLine* const flp = flpOf(ifp);
        AstNodeExpr* const condp = lowerExpr(ifp->child("condition"), bitDType(flp));
        if (!condp) {
            fallsThroughp = false;
            return nullptr;
        }
        bool thenFalls = true;
        AstNode* const thensp
            = lowerProcChainWithTail(childEls(ifp->child("sequential_statement_chain")), 0, tail,
                                     tailIndex, mode, thenFalls);
        AstNode* elsesp = nullptr;
        bool elseFalls = true;
        const Node* const elsep = ifp->child("else_clause");
        if (!elsep) {
            elsesp = lowerProcChain(tail, tailIndex, mode, elseFalls);
        } else if (elsep->attr("kind") == "elsif" && elsep->child("condition")) {
            elsesp = lowerProcIf(elsep, tail, tailIndex, mode, elseFalls);
        } else {
            elsesp = lowerProcChainWithTail(childEls(elsep->child("sequential_statement_chain")),
                                            0, tail, tailIndex, mode, elseFalls);
        }
        fallsThroughp = thenFalls || elseFalls;
        return new AstIf{flp, condp, thensp, elsesp};
    }

    //! Lower a case statement, appending the following statements to each alternative
    AstNode* lowerProcCase(const Node* casep, const std::vector<const Node*>& tail,
                           size_t tailIndex, AssignMode mode, bool& fallsThroughp) {
        FileLine* const flp = flpOf(casep);
        AstNodeExpr* const selectorp = lowerExpr(casep->child("expression"), nullptr);
        if (!selectorp) {
            fallsThroughp = false;
            return nullptr;
        }
        AstCaseItem* itemsp = nullptr;
        AstCaseItem* currentp = nullptr;
        fallsThroughp = false;
        for (const Node* const choicep :
             childEls(casep->child("case_statement_alternative_chain"))) {
            const bool sameAlternative = choicep->attr("same_alternative_flag") == "true";
            const std::string kind = choicep->attr("kind");
            const auto lowerBody = [&]() -> AstNode* {
                bool altFalls = true;
                AstNode* const bodyp
                    = lowerProcChainWithTail(childEls(choicep->child("associated_chain")), 0, tail,
                                             tailIndex, mode, altFalls);
                fallsThroughp = fallsThroughp || altFalls;
                return bodyp;
            };
            if (kind == "choice_by_others") {
                currentp = new AstCaseItem{flp, nullptr, lowerBody()};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
                continue;
            }
            std::vector<AstConst*> conds
                = lowerChoiceConditions(choicep, selectorp->dtypep(), "case");
            if (conds.empty()) continue;
            if (!sameAlternative || !currentp) {
                AstConst* const firstp = conds.front();
                conds.erase(conds.begin());
                currentp = new AstCaseItem{flp, firstp, lowerBody()};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
            }
            for (AstConst* const condp : conds) currentp->addCondsp(condp);
        }
        return new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
    }

    AstNodeExpr* lowerRecordAggregate(const Node* nodep, const RecordInfo& info) {
        FileLine* const flp = flpOf(nodep);
        std::vector<const Node*> exprs(info.members.size(), nullptr);
        size_t position = 0;
        for (const Node* const choicep : childEls(nodep->child("association_choices_chain"))) {
            const std::string choiceKind = choicep->attr("kind");
            const Node* const exprp = choicep->child("associated_expr");
            if (choiceKind == "choice_by_name") {
                const Node* const namep = choicep->child("choice_name");
                const std::string name
                    = namep ? vhdlFold(namep->attr("identifier")) : std::string{};
                size_t index = info.members.size();
                for (size_t i = 0; i < info.members.size(); ++i) {
                    if (vhdlFold(info.members[i].name) == name) index = i;
                }
                if (index == info.members.size()) {
                    error(nodep, "VHDL record aggregate member '" + name + "' not found");
                    return nullptr;
                }
                exprs[index] = exprp;
            } else if (choiceKind == "choice_by_none") {
                if (position >= exprs.size()) {
                    error(nodep, "VHDL record aggregate has too many elements");
                    return nullptr;
                }
                exprs[position++] = exprp;
            } else {
                unsupported(choicep, "aggregate choice '" + choiceKind + "'");
                return nullptr;
            }
        }
        AstNodeExpr* resultp = nullptr;
        for (size_t i = info.members.size(); i-- > 0;) {
            const RecordMember& member = info.members[i];
            if (!exprs[i]) {
                error(nodep, "VHDL record aggregate is missing member '" + member.name + "'");
                return nullptr;
            }
            AstNodeExpr* const valuep = lowerExpr(exprs[i], member.dtypep);
            if (!valuep) return nullptr;
            resultp = resultp ? static_cast<AstNodeExpr*>(new AstConcat{flp, valuep, resultp})
                              : valuep;
        }
        if (!resultp) return makeConst(flp, info.dtypep, 0);
        resultp->dtypep(info.dtypep);
        return resultp;
    }

    //! A lowered integer expression split into base*scale + offset
    struct LinearSplit final {
        AstNodeExpr* basep = nullptr;  // Varying part, null when fully constant
        int64_t scale = 0;  // Multiplier of basep
        int64_t offset = 0;  // Constant part
    };

    //! Split a linear integer expression; false when two different bases are mixed
    static bool splitLinear(AstNodeExpr* exprp, LinearSplit& out) {
        if (AstConst* const constp = VN_CAST(exprp, Const)) {
            out.offset = constp->num().toSInt();
            return true;
        }
        if (AstAdd* const addp = VN_CAST(exprp, Add)) {
            LinearSplit lhss;
            LinearSplit rhss;
            if (!splitLinear(addp->lhsp(), lhss) || !splitLinear(addp->rhsp(), rhss)) {
                return false;
            }
            out.offset = lhss.offset + rhss.offset;
            if (lhss.basep && rhss.basep) {
                if (lhss.scale != rhss.scale || !lhss.basep->sameTree(rhss.basep)) return false;
            }
            out.basep = lhss.basep ? lhss.basep : rhss.basep;
            out.scale = lhss.basep ? lhss.scale : rhss.scale;
            return true;
        }
        if (AstSub* const subp = VN_CAST(exprp, Sub)) {
            LinearSplit lhss;
            LinearSplit rhss;
            if (!splitLinear(subp->lhsp(), lhss) || !splitLinear(subp->rhsp(), rhss)) {
                return false;
            }
            if (rhss.basep) {
                if (!lhss.basep || lhss.scale != rhss.scale || !lhss.basep->sameTree(rhss.basep)) {
                    return false;
                }
            }
            out.basep = lhss.basep;
            out.scale = lhss.scale;
            out.offset = lhss.offset - rhss.offset;
            return true;
        }
        if (AstMul* const mulp = VN_CAST(exprp, Mul)) {
            LinearSplit lhss;
            LinearSplit rhss;
            if (!splitLinear(mulp->lhsp(), lhss) || !splitLinear(mulp->rhsp(), rhss)) {
                return false;
            }
            if (!lhss.basep && !rhss.basep) {
                out.basep = nullptr;
                out.scale = 0;
                out.offset = lhss.offset * rhss.offset;
                return true;
            }
            if (lhss.basep && rhss.basep) return false;
            const LinearSplit& dyn = lhss.basep ? lhss : rhss;
            const LinearSplit& con = lhss.basep ? rhss : lhss;
            out.basep = dyn.basep;
            out.scale = dyn.scale * con.offset;
            out.offset = dyn.offset * con.offset;
            return true;
        }
        if (AstNegate* const negp = VN_CAST(exprp, Negate)) {
            LinearSplit vals;
            if (!splitLinear(negp->lhsp(), vals)) return false;
            out.basep = vals.basep;
            out.scale = -vals.scale;
            out.offset = -vals.offset;
            return true;
        }
        out.basep = exprp;
        out.scale = 1;
        out.offset = 0;
        return true;
    }

    //! Difference of two slice bounds plus one, when the varying parts cancel
    static bool staticRangeWidth(AstNodeExpr* highp, AstNodeExpr* lowp, int64_t& width) {
        LinearSplit highs;
        LinearSplit lows;
        if (!splitLinear(highp, highs) || !splitLinear(lowp, lows)) return false;
        if (highs.basep || lows.basep) {
            if (!highs.basep || !lows.basep || highs.scale != lows.scale
                || !highs.basep->sameTree(lows.basep)) {
                return false;
            }
        }
        width = highs.offset - lows.offset + 1;
        return true;
    }

    //! Lower both sides of a comparison to a common width. VHDL compares operands of
    //! different lengths by extending the shorter one, so the post-width AST needs a
    //! single operand width.
    void lowerComparison(const Node* nodep, AstNodeExpr*& lhsp, AstNodeExpr*& rhsp) {
        lhsp = lowerExpr(nodep->child("left"), nullptr);
        if (!lhsp) return;
        // The left side's type orients a literal on the right side
        rhsp = lowerExpr(nodep->child("right"), lhsp->dtypep());
        if (!rhsp) return;
        AstBasicDType* const lbasicp = VN_CAST(lhsp->dtypep()->skipRefp(), BasicDType);
        const AstBasicDType* const rbasicp = VN_CAST(rhsp->dtypep()->skipRefp(), BasicDType);
        if (!lbasicp || !rbasicp || lbasicp->keyword() == VBasicDTypeKwd::INT
            || rbasicp->keyword() == VBasicDTypeKwd::INT)
            return;
        const int width = std::max(lbasicp->width(), rbasicp->width());
        if (lbasicp->width() == width && rbasicp->width() == width) return;
        AstNodeDType* const dtp = m_rootp->typeTablep()->findLogicBitDType(
            flpOf(nodep), lbasicp->keyword(), width, width, lbasicp->numeric());
        lhsp = resizeExpr(lhsp, dtp);
        rhsp = resizeExpr(rhsp, dtp);
    }

    //! Lower an array aggregate: positional, named (single or range) and `others` choices
    AstNodeExpr* lowerArrayAggregate(const Node* nodep, AstNodeDType* contextp) {
        FileLine* const flp = flpOf(nodep);
        VhdlDTypeSpec layout;
        const bool haveLayout = arraySpec(nodep, layout);
        AstNodeDType* const targetDtp = contextp ? contextp : typeRefDType(nodep);
        if (!targetDtp || targetDtp->width() <= 0) return nullptr;
        const int elemWidth = haveLayout ? layout.elemWidth : 1;
        const int totalWidth = targetDtp->width();
        if (elemWidth <= 0 || totalWidth % elemWidth != 0) return nullptr;
        const int elements = totalWidth / elemWidth;
        int left = 0;
        int right = elements - 1;
        bool ascending = true;
        if (haveLayout) {
            left = layout.left;
            right = layout.right;
            ascending = layout.ascending;
        } else if (const AstBasicDType* const basicp
                   = VN_CAST(targetDtp->skipRefp(), BasicDType)) {
            left = basicp->left();
            right = basicp->right();
            ascending = basicp->ascending();
        }
        // The rightmost element is the least significant one
        const int arrayRight = right;
        AstNodeDType* elemDtp = nullptr;
        if (elemWidth > 1) {
            VBasicDTypeKwd kwd = VBasicDTypeKwd::LOGIC;
            VSigning signing = VSigning::UNSIGNED;
            if (haveLayout) {
                kwd = layout.kwd;
                signing = layout.signing;
            }
            elemDtp = m_rootp->typeTablep()->findLogicBitDType(flp, kwd, elemWidth, elemWidth,
                                                               signing);
        }
        // A single `others` choice is a replication; avoid one node per element
        const std::vector<const Node*> choices
            = childEls(nodep->child("association_choices_chain"));
        if (choices.size() == 1 && choices.front()->attr("kind") == "choice_by_others") {
            AstNodeExpr* const elemp
                = lowerExpr(choices.front()->child("associated_expr"), elemDtp);
            if (!elemp) return nullptr;
            const int width = elemp->dtypep()->width();
            if (width <= 0 || totalWidth % width != 0) return nullptr;
            AstReplicate* const repp
                = new AstReplicate{flp, elemp, static_cast<uint32_t>(totalWidth / width)};
            repp->dtypep(targetDtp);
            return repp;
        }
        const auto slotOf = [&](int64_t index) -> int {
            const int64_t slot = ascending ? static_cast<int64_t>(arrayRight) - index
                                           : index - static_cast<int64_t>(arrayRight);
            if (slot < 0 || slot >= elements) return -1;
            return static_cast<int>(slot);
        };
        std::vector<AstNodeExpr*> exprs(static_cast<size_t>(elements), nullptr);
        const Node* othersp = nullptr;
        int position = 0;
        for (const Node* const choicep : childEls(nodep->child("association_choices_chain"))) {
            const Node* const assocp = choicep->child("associated_expr");
            if (!assocp) return nullptr;
            const std::string choiceKind = choicep->attr("kind");
            if (choiceKind == "choice_by_others") {
                othersp = assocp;
            } else if (choiceKind == "choice_by_expression") {
                int64_t index = 0;
                if (!evalConst(choicep->child("choice_expression"), index)) return nullptr;
                const int slot = slotOf(index);
                if (slot < 0) return nullptr;
                exprs[slot] = lowerExpr(assocp, elemDtp);
                if (!exprs[slot]) return nullptr;
            } else if (choiceKind == "choice_by_range") {
                const Node* const rangep = derefNode(choicep->child("choice_range"));
                int64_t leftValue = 0;
                int64_t rightValue = 0;
                if (!rangep || !evalConst(rangeBound(rangep, true), leftValue)
                    || !evalConst(rangeBound(rangep, false), rightValue)) {
                    return nullptr;
                }
                const int64_t lowv = std::min(leftValue, rightValue);
                const int64_t highv = std::max(leftValue, rightValue);
                for (int64_t index = lowv; index <= highv; ++index) {
                    const int slot = slotOf(index);
                    if (slot < 0) return nullptr;
                    exprs[slot] = lowerExpr(assocp, elemDtp);
                    if (!exprs[slot]) return nullptr;
                }
            } else if (choiceKind == "choice_by_none") {
                const int index = ascending ? left + position : left - position;
                ++position;
                const int slot = slotOf(index);
                if (slot < 0) return nullptr;
                exprs[slot] = lowerExpr(assocp, elemDtp);
                if (!exprs[slot]) return nullptr;
            } else {
                return nullptr;
            }
        }
        if (othersp) {
            for (AstNodeExpr*& valuep : exprs) {
                if (valuep) continue;
                valuep = lowerExpr(othersp, elemDtp);
                if (!valuep) return nullptr;
            }
        }
        AstNodeExpr* resultp = nullptr;
        for (size_t i = 0; i < exprs.size(); ++i) {
            AstNodeExpr* const valuep = exprs[i];
            if (!valuep) {
                error(nodep, "VHDL aggregate is missing elements");
                return nullptr;
            }
            resultp = resultp ? static_cast<AstNodeExpr*>(new AstConcat{flp, valuep, resultp})
                              : valuep;
        }
        resultp->dtypep(targetDtp);
        return resultp;
    }

    AstNodeExpr* lowerExpr(const Node* nodep, AstNodeDType* contextp) {
        if (!nodep) return nullptr;
        FileLine* const flp = flpOf(nodep);
        const std::string kind = nodep->attr("kind");

        if (kind == "simple_name" && m_inlineEnvp) {
            const auto it = m_inlineEnvp->find(vhdlFold(nodep->attr("identifier")));
            if (it != m_inlineEnvp->end()) {
                if (!it->second.readable) {
                    unsupported(nodep, "read of an out procedure parameter");
                    return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
                }
                AstNodeExpr* const valuep = it->second.exprp->cloneTree(false);
                if (it->second.lvalue) setReadAccess(valuep);
                return valuep;
            }
        }
        if (kind == "simple_name") {
            const std::string id = vhdlFold(nodep->attr("identifier"));
            if (id == "true" || id == "false") {
                return makeConst(flp, contextp ? contextp : bitDType(flp), id == "true" ? 1U : 0U);
            }
            int64_t value = 0;
            if (evalConst(nodep, value)) {
                return makeConst(flp, contextp ? contextp : intDType(flp),
                                 static_cast<uint64_t>(value));
            }
            if (const Node* const refp = nodep->child("named_entity")) {
                if (refp->has("ref")) {
                    const Node* const declp = nodeById(refp->attr("ref"));
                    if (declp && declp->attr("kind") == "object_alias_declaration") {
                        return lowerExpr(declp->child("name"), contextp);
                    }
                }
            }
            // Non-scalar constants resolve to their declaration's default value
            if (const Node* const refp = nodep->child("named_entity")) {
                if (refp->has("ref")) {
                    const Node* const declp = nodeById(refp->attr("ref"));
                    if (declp) {
                        const std::string declKind = declp->attr("kind");
                        if (declKind == "constant_declaration"
                            || declKind == "interface_constant_declaration") {
                            if (const Node* const defp = declp->child("default_value")) {
                                return lowerExpr(defp, contextp);
                            }
                        }
                    }
                }
            }
            // Enumeration literal: resolve the analyzed literal declaration
            if (const Node* const refp = nodep->child("named_entity")) {
                if (refp->has("ref")) {
                    const Node* const declp = nodeById(refp->attr("ref"));
                    if (declp && declp->attr("kind") == "enumeration_literal") {
                        AstNodeDType* dtp = contextp ? contextp : enumDTypeOfLiteral(declp);
                        if (!dtp) dtp = bitDType(flp);
                        const int64_t pos
                            = std::strtoll(declp->attr("enum_pos").c_str(), nullptr, 10);
                        return makeConst(flp, dtp, static_cast<uint64_t>(pos));
                    }
                }
            }
        }
        if (kind == "simple_name" || kind == "indexed_name" || kind == "slice_name"
            || kind == "selected_element") {
            return lowerName(nodep, false);
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
        if (kind == "left_type_attribute" || kind == "right_type_attribute"
            || kind == "high_type_attribute" || kind == "low_type_attribute") {
            if (const Node* const defp
                = typeDefOfKind(nodep->child("prefix"), "enumeration_type_definition")) {
                const int lastp
                    = static_cast<int>(childEls(defp->child("enumeration_literal_list")).size())
                      - 1;
                const bool isLast
                    = kind == "right_type_attribute" || kind == "high_type_attribute";
                return makeConst(flp, enumDType(defp), static_cast<uint64_t>(isLast ? lastp : 0));
            }
        }
        if (kind == "left_attribute" || kind == "right_attribute" || kind == "high_attribute"
            || kind == "low_attribute" || kind == "length_attribute"
            || kind == "ascending_attribute" || kind == "left_array_attribute"
            || kind == "right_array_attribute" || kind == "high_array_attribute"
            || kind == "low_array_attribute" || kind == "length_array_attribute"
            || kind == "ascending_array_attribute") {
            int64_t value = 0;
            if (!attrValue(nodep, value)) {
                unsupported(nodep, "attribute " + kind);
                return makeConst(flp, contextp ? contextp : intDType(flp), 0);
            }
            const bool isAscending
                = kind == "ascending_attribute" || kind == "ascending_array_attribute";
            AstNodeDType* const dtp = isAscending ? (contextp ? contextp : bitDType(flp))
                                                  : (contextp ? contextp : intDType(flp));
            return makeConst(flp, dtp, static_cast<uint64_t>(value));
        }
        if (kind == "pos_attribute" || kind == "succ_attribute" || kind == "pred_attribute"
            || kind == "val_attribute") {
            const Node* const defp
                = typeDefOfKind(nodep->child("prefix"), "enumeration_type_definition");
            if (!defp) {
                unsupported(nodep, "attribute " + kind);
                return makeConst(flp, contextp ? contextp : intDType(flp), 0);
            }
            AstNodeDType* const enump = enumDType(defp);
            if (kind == "val_attribute") {
                AstNodeExpr* const indexp = lowerExpr(nodep->child("parameter"), nullptr);
                if (!indexp) return nullptr;
                return resizeExpr(indexp, enump);
            }
            AstNodeExpr* const valuep = lowerExpr(nodep->child("parameter"), enump);
            if (!valuep) return nullptr;
            if (kind == "pos_attribute") return resizeExpr(valuep, intDType(flp));
            AstNodeExpr* const onep = makeConst(flp, enump, 1);
            AstNodeExpr* const resultp
                = kind == "succ_attribute"
                      ? static_cast<AstNodeExpr*>(new AstAdd{flp, valuep, onep})
                      : static_cast<AstNodeExpr*>(new AstSub{flp, valuep, onep});
            resultp->dtypep(enump);
            return resultp;
        }
        if (kind == "enumeration_literal") {
            const std::string id = vhdlFold(nodep->attr("identifier"));
            if (id == "true" || id == "false") {
                return makeConst(flp, contextp ? contextp : bitDType(flp), id == "true" ? 1U : 0U);
            }
            AstNodeDType* dtp = contextp ? contextp : enumDTypeOfLiteral(nodep);
            if (!dtp) {
                unsupported(nodep, "enumeration literal " + nodep->attr("identifier"));
                return makeConst(flp, bitDType(flp), 0);
            }
            const int64_t pos = std::strtoll(nodep->attr("enum_pos").c_str(), nullptr, 10);
            return makeConst(flp, dtp, static_cast<uint64_t>(pos));
        }
        if (kind == "string_literal8") {
            const Node* const strp = nodep->child("string8_id");
            const std::string content = strp ? strp->attr("content") : std::string{};
            AstNodeDType* dtp = contextp;
            if (!dtp) dtp = typeRefDType(nodep);
            if (!dtp) dtp = bitDType(flp);
            if (content.empty()) {
                unsupported(nodep, "empty string literal");
                return makeConst(flp, dtp, 0);
            }
            const int contentWidth = static_cast<int>(content.size());
            for (const char ch : content) {
                if (ch != '0' && ch != '1') {
                    unsupported(nodep, "non-binary string literal");
                    return makeConst(flp, dtp, 0);
                }
            }
            if (contentWidth > dtp->width()) {
                error(nodep, "VHDL string literal length does not match its context");
                return makeConst(flp, dtp, 0);
            }
            // Build the literal bit by bit: a fixed-width accumulator truncates wide literals
            V3Number num{flp, dtp->width(), 0};
            const int leading = contentWidth - 1;
            for (int i = 0; i < contentWidth; ++i) num.setBit(leading - i, content[i]);
            // A numeric_std operation may resize a shorter literal to the operand width
            if (contentWidth < dtp->width() && dtp->isSigned() && content.front() == '1') {
                for (int bit = contentWidth; bit < dtp->width(); ++bit) num.setBit(bit, '1');
            }
            AstConst* const constp = new AstConst{flp, num};
            constp->dtypep(dtp);
            return constp;
        }
        if (kind == "addition_operator" || kind == "subtraction_operator"
            || kind == "substraction_operator" || kind == "multiplication_operator"
            || kind == "division_operator" || kind == "modulus_operator"
            || kind == "remainder_operator" || kind == "exponentiation_operator"
            || kind == "and_operator" || kind == "or_operator" || kind == "xor_operator"
            || kind == "nand_operator" || kind == "nor_operator" || kind == "xnor_operator") {
            AstNodeExpr* lhsp = lowerExpr(nodep->child("left"), contextp);
            if (!lhsp) return nullptr;
            AstNodeDType* const lhsDtp = lhsp->dtypep();
            AstBasicDType* const lbasicp = VN_CAST(lhsDtp->skipRefp(), BasicDType);
            const bool intOp = !lbasicp || lbasicp->keyword() == VBasicDTypeKwd::INT;
            const bool powOp = kind == "exponentiation_operator";
            // A numeric_std exponent is a natural, not a vector operand
            AstNodeExpr* rhsp
                = lowerExpr(nodep->child("right"), intOp || powOp ? intDType(flp) : lhsDtp);
            if (!rhsp) return nullptr;
            AstNodeDType* opDtp = lhsDtp;
            int resultWidth = lhsDtp->width();
            if (!intOp) {
                const int lwidth = lhsDtp->width();
                const int rwidth = rhsp->dtypep()->width();
                // numeric_std result lengths: product sums the operand lengths, division
                // and exponentiation keep the left length, other operators take the wider
                if (kind == "multiplication_operator") {
                    resultWidth = lwidth + rwidth;
                } else if (kind == "division_operator" || kind == "remainder_operator"
                           || kind == "modulus_operator" || powOp) {
                    resultWidth = lwidth;
                } else {
                    resultWidth = std::max(lwidth, rwidth);
                }
                // A post-width AST needs equally wide operands. Keep a wider divisor or
                // remainder operand whole and extract the result width below.
                const int opWidth
                    = powOp ? lwidth : std::max(resultWidth, std::max(lwidth, rwidth));
                opDtp = m_rootp->typeTablep()->findLogicBitDType(flp, lbasicp->keyword(), opWidth,
                                                                 opWidth, lbasicp->numeric());
                lhsp = resizeExpr(lhsp, opDtp);
                if (!powOp) rhsp = resizeExpr(rhsp, opDtp);
            }
            const bool signedp = opDtp->isSigned();
            AstNodeExpr* resultp = nullptr;
            if (kind == "addition_operator") {
                resultp = new AstAdd{flp, lhsp, rhsp};
            } else if (kind == "subtraction_operator" || kind == "substraction_operator") {
                resultp = new AstSub{flp, lhsp, rhsp};
            } else if (kind == "multiplication_operator") {
                resultp = new AstMul{flp, lhsp, rhsp};
            } else if (kind == "division_operator") {
                resultp = signedp ? static_cast<AstNodeExpr*>(new AstDivS{flp, lhsp, rhsp})
                                  : static_cast<AstNodeExpr*>(new AstDiv{flp, lhsp, rhsp});
            } else if (kind == "remainder_operator") {
                resultp = signedp ? static_cast<AstNodeExpr*>(new AstModDivS{flp, lhsp, rhsp})
                                  : static_cast<AstNodeExpr*>(new AstModDiv{flp, lhsp, rhsp});
            } else if (kind == "modulus_operator") {
                // VHDL mod takes the sign of the divisor, unlike the C remainder
                AstNodeExpr* const remp
                    = signedp ? static_cast<AstNodeExpr*>(new AstModDivS{flp, lhsp, rhsp})
                              : static_cast<AstNodeExpr*>(new AstModDiv{flp, lhsp, rhsp});
                remp->dtypep(opDtp);
                if (!signedp) {
                    resultp = remp;
                } else {
                    // mod = rem + b when rem is nonzero and the signs differ
                    AstNodeExpr* const addRemp = remp->cloneTree(false);
                    AstNodeExpr* const signRemp = remp->cloneTree(false);
                    AstNodeExpr* const testRemp = remp->cloneTree(false);
                    AstNodeExpr* const signZerop = makeConst(flp, opDtp, 0);
                    AstNodeExpr* const testZerop = makeConst(flp, opDtp, 0);
                    AstNodeExpr* const remNegp = new AstLtS{flp, signRemp, signZerop};
                    remNegp->dtypep(bitDType(flp));
                    AstNodeExpr* const rhsNegp
                        = new AstLtS{flp, rhsp->cloneTree(false), makeConst(flp, opDtp, 0)};
                    rhsNegp->dtypep(bitDType(flp));
                    AstNodeExpr* const diffSignp = new AstXor{flp, remNegp, rhsNegp};
                    diffSignp->dtypep(bitDType(flp));
                    AstNodeExpr* const nonzerop = new AstNeq{flp, testRemp, testZerop};
                    nonzerop->dtypep(bitDType(flp));
                    AstNodeExpr* const adjustp = new AstAnd{flp, nonzerop, diffSignp};
                    adjustp->dtypep(bitDType(flp));
                    AstNodeExpr* const sump = new AstAdd{flp, addRemp, rhsp->cloneTree(false)};
                    sump->dtypep(opDtp);
                    resultp = new AstCond{flp, adjustp, sump, remp};
                }
            } else if (kind == "exponentiation_operator") {
                resultp = new AstPow{flp, lhsp, rhsp};
            } else if (kind == "and_operator") {
                resultp = new AstAnd{flp, lhsp, rhsp};
            } else if (kind == "or_operator") {
                resultp = new AstOr{flp, lhsp, rhsp};
            } else if (kind == "xor_operator") {
                resultp = new AstXor{flp, lhsp, rhsp};
            } else if (kind == "nand_operator") {
                AstNodeExpr* const andp = new AstAnd{flp, lhsp, rhsp};
                andp->dtypep(opDtp);
                resultp = new AstNot{flp, andp};
            } else if (kind == "nor_operator") {
                AstNodeExpr* const orp = new AstOr{flp, lhsp, rhsp};
                orp->dtypep(opDtp);
                resultp = new AstNot{flp, orp};
            } else {
                AstNodeExpr* const xorp = new AstXor{flp, lhsp, rhsp};
                xorp->dtypep(opDtp);
                resultp = new AstNot{flp, xorp};
            }
            resultp->dtypep(opDtp);
            if (resultWidth != opDtp->width()) {
                AstSel* const selp = new AstSel{flp, resultp, 0, resultWidth};
                selp->dtypep(m_rootp->typeTablep()->findLogicBitDType(
                    flp, lbasicp->keyword(), resultWidth, resultWidth, lbasicp->numeric()));
                return selp;
            }
            return resultp;
        }
        if (kind == "less_than_operator" || kind == "less_than_or_equal_operator"
            || kind == "greater_than_operator" || kind == "greater_than_or_equal_operator") {
            AstNodeExpr* lhsp = nullptr;
            AstNodeExpr* rhsp = nullptr;
            lowerComparison(nodep, lhsp, rhsp);
            if (!lhsp || !rhsp) return nullptr;
            const bool signedp = lhsp->dtypep()->isSigned();
            AstNodeExpr* resultp = nullptr;
            if (kind == "less_than_operator") {
                resultp = signedp ? static_cast<AstNodeExpr*>(new AstLtS{flp, lhsp, rhsp})
                                  : static_cast<AstNodeExpr*>(new AstLt{flp, lhsp, rhsp});
            } else if (kind == "less_than_or_equal_operator") {
                resultp = signedp ? static_cast<AstNodeExpr*>(new AstLteS{flp, lhsp, rhsp})
                                  : static_cast<AstNodeExpr*>(new AstLte{flp, lhsp, rhsp});
            } else if (kind == "greater_than_operator") {
                resultp = signedp ? static_cast<AstNodeExpr*>(new AstGtS{flp, lhsp, rhsp})
                                  : static_cast<AstNodeExpr*>(new AstGt{flp, lhsp, rhsp});
            } else {
                resultp = signedp ? static_cast<AstNodeExpr*>(new AstGteS{flp, lhsp, rhsp})
                                  : static_cast<AstNodeExpr*>(new AstGte{flp, lhsp, rhsp});
            }
            resultp->dtypep(bitDType(flp));
            return resultp;
        }
        if (kind == "absolute_operator") {
            AstNodeExpr* const valuep = lowerExpr(nodep->child("operand"), contextp);
            if (!valuep) return nullptr;
            AstNodeDType* const dtp = valuep->dtypep();
            if (!dtp->isSigned()) return valuep;
            AstNodeExpr* const negp = new AstNegate{flp, valuep->cloneTree(false)};
            negp->dtypep(dtp);
            AstNodeExpr* const condp
                = new AstLtS{flp, valuep->cloneTree(false), makeConst(flp, dtp, 0)};
            condp->dtypep(bitDType(flp));
            AstCond* const resultp = new AstCond{flp, condp, negp, valuep};
            resultp->dtypep(dtp);
            return resultp;
        }
        if (kind == "sll_operator" || kind == "srl_operator" || kind == "sla_operator"
            || kind == "sra_operator") {
            AstNodeExpr* const lhsp = lowerExpr(nodep->child("left"), nullptr);
            if (!lhsp) return nullptr;
            AstNodeExpr* const amountp = lowerExpr(nodep->child("right"), nullptr);
            if (!amountp) return nullptr;
            AstNodeDType* const dtp = lhsp->dtypep();
            AstNodeExpr* resultp = nullptr;
            // numeric_std defines sla as sll and srl as a logical shift even for signed
            if (kind == "sll_operator" || kind == "sla_operator") {
                resultp = new AstShiftL{flp, lhsp, amountp, dtp->width()};
            } else if (kind == "sra_operator" && dtp->isSigned()) {
                resultp = new AstShiftRS{flp, lhsp, amountp, dtp->width()};
            } else {
                resultp = new AstShiftR{flp, lhsp, amountp, dtp->width()};
            }
            resultp->dtypep(dtp);
            return resultp;
        }
        if (kind == "concatenation_operator") {
            AstNodeExpr* const lhsp = lowerExpr(nodep->child("left"), nullptr);
            if (!lhsp) return nullptr;
            AstNodeExpr* const rhsp = lowerExpr(nodep->child("right"), nullptr);
            if (!rhsp) return nullptr;
            const int width = lhsp->dtypep()->width() + rhsp->dtypep()->width();
            const bool fourstate = lhsp->dtypep()->isFourstate() || rhsp->dtypep()->isFourstate();
            AstConcat* const concatp = new AstConcat{flp, lhsp, rhsp};
            concatp->dtypep(m_rootp->typeTablep()->findLogicBitDType(
                flp, fourstate ? VBasicDTypeKwd::LOGIC : VBasicDTypeKwd::BIT, width, width,
                VSigning::UNSIGNED));
            return concatp;
        }
        if (kind == "negation_operator") {
            AstNodeExpr* const valuep = lowerExpr(nodep->child("operand"), contextp);
            if (!valuep) return nullptr;
            AstNodeExpr* const negp = new AstNegate{flp, valuep};
            negp->dtypep(valuep->dtypep());
            return negp;
        }
        if (kind == "not_operator") {
            AstNodeExpr* const operandp = lowerExpr(nodep->child("operand"), contextp);
            if (!operandp) return nullptr;
            AstNot* const notp = new AstNot{flp, operandp};
            notp->dtypep(operandp->dtypep());
            return notp;
        }
        if (kind == "equality_operator" || kind == "inequality_operator") {
            AstNodeExpr* lhsp = nullptr;
            AstNodeExpr* rhsp = nullptr;
            lowerComparison(nodep, lhsp, rhsp);
            if (!lhsp || !rhsp) return nullptr;
            AstNodeExpr* const resultp
                = kind == "equality_operator"
                      ? static_cast<AstNodeExpr*>(new AstEq{flp, lhsp, rhsp})
                      : static_cast<AstNodeExpr*>(new AstNeq{flp, lhsp, rhsp});
            resultp->dtypep(bitDType(flp));
            return resultp;
        }
        if (kind == "qualified_expression") {
            // The context constrains the operand; otherwise use the expression's own
            // (possibly constrained) type.  A literal subtype only carries its element
            // type, so a `signed` qualification must restore the signing.
            AstNodeDType* dtp = contextp;
            if (!dtp) {
                dtp = typeRefDType(nodep);
                const Node* const markp = nodep->child("type_mark");
                const std::string mark
                    = markp ? vhdlFold(markp->attr("identifier")) : std::string{};
                AstBasicDType* const basicp = dtp ? VN_CAST(dtp->skipRefp(), BasicDType) : nullptr;
                if (basicp && !basicp->isSigned()
                    && (mark == "signed" || mark == "unresolved_signed")) {
                    dtp = m_rootp->typeTablep()->findLogicBitDType(
                        flp, basicp->keyword(),
                        VNumRange{basicp->left(), basicp->right(), basicp->ascending()},
                        basicp->width(), VSigning::SIGNED);
                }
            }
            return lowerExpr(nodep->child("expression"), dtp);
        }
        if (kind == "type_conversion") {
            AstNodeExpr* const innerp = lowerExpr(nodep->child("expression"), contextp);
            if (!innerp) return nullptr;
            if (!contextp || innerp->dtypep() == contextp) return innerp;
            return resizeExpr(innerp, contextp);
        }
        if (kind == "aggregate") {
            RecordInfo recInfo;
            if (recordLayoutOfName(nodep, recInfo)) {
                return lowerRecordAggregate(nodep, recInfo);
            }
            if (AstNodeExpr* const resultp = lowerArrayAggregate(nodep, contextp)) {
                return resultp;
            }
            unsupported(nodep, "aggregate of a non-record type");
            return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
        }
        if (kind == "simple_aggregate") {
            // GHDL folds locally static expressions into an aggregate that retains the
            // originating call; lower that call instead
            if (const Node* const originp = nodep->child("literal_origin")) {
                return lowerExpr(originp, contextp);
            }
            unsupported(nodep, "static aggregate");
            return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
        }
        if (kind == "function_call") { return lowerCall(nodep, contextp); }
        unsupported(nodep, "expression '" + kind + "'");
        return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
    }

    // ---- Statements

    //! Target of an assignment; desugared assignments reference the outer target by id
    const Node* targetNode(const Node* stmtp) const {
        const Node* targetp = stmtp->child("target");
        if (targetp && !targetp->has("kind") && targetp->has("ref")) {
            targetp = nodeById(targetp->attr("ref"));
        }
        return targetp;
    }

    AstNode* lowerAssignment(const Node* stmtp, AssignMode mode) {
        FileLine* const flp = flpOf(stmtp);
        AstNodeExpr* const lhsp = lowerLValue(stmtp, targetNode(stmtp));
        if (!lhsp) return nullptr;
        if (mode != AssignMode::CONT) {
            // V3Tristate/V3Inst model inout ports as continuously driven; a procedural
            // assignment would reach DFG as a non-read reference
            bool inoutPort = false;
            lhsp->foreach([&](AstVarRef* refp) {
                AstVar* const varp = refp->varp();
                if (varp->isIO() && varp->direction().isInout()) inoutPort = true;
            });
            if (inoutPort) {
                unsupported(stmtp, "procedural assignment to an inout port");
                return nullptr;
            }
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
        AstNodeExpr* const rhsp = lowerExpr(wep, lhsp->dtypep());
        if (!rhsp) return nullptr;
        if (mode == AssignMode::DELAYED) return new AstAssignDly{flp, lhsp, rhsp};
        if (mode == AssignMode::BLOCKING) return new AstAssign{flp, lhsp, rhsp};
        return new AstAssignW{flp, lhsp, rhsp};
    }

    //! Lower an elsif/else clause chain attached to an if statement
    AstNode* lowerElse(const Node* elsep, AssignMode mode) {
        const std::string kind = elsep->attr("kind");
        const Node* const condp = elsep->child("condition");
        if (kind == "else" || (kind == "elsif" && !condp)) {
            return lowerStmtChain(elsep->child("sequential_statement_chain"), mode);
        }
        if (kind == "elsif") {
            FileLine* const flp = flpOf(elsep);
            AstNodeExpr* const condExprp = lowerExpr(condp, bitDType(flp));
            AstNode* const thensp
                = lowerStmtChain(elsep->child("sequential_statement_chain"), mode);
            AstNode* const elsesp = elsep->child("else_clause")
                                        ? lowerElse(elsep->child("else_clause"), mode)
                                        : nullptr;
            return new AstIf{flp, condExprp, thensp, elsesp};
        }
        unsupported(elsep, "else clause form '" + kind + "'");
        return nullptr;
    }

    AstNode* lowerVariableAssignment(const Node* stmtp) {
        FileLine* const flp = flpOf(stmtp);
        AstNodeExpr* const lhsp = lowerLValue(stmtp, stmtp->child("target"));
        if (!lhsp) return nullptr;
        const Node* const exprp = stmtp->child("expression");
        if (!exprp) {
            unsupported(stmtp, "variable assignment without an expression");
            return nullptr;
        }
        AstNodeExpr* const rhsp = lowerExpr(exprp, lhsp->dtypep());
        if (!rhsp) return nullptr;
        // Variable assignment is immediate, unlike signal assignment
        return new AstAssign{flp, lhsp, rhsp};
    }

    //! Lower a case or selected-assignment choice to constant conditions.  A range choice
    //! expands to one condition per value; `others` is handled by the caller.
    std::vector<AstConst*> lowerChoiceConditions(const Node* choicep, AstNodeDType* dtypep,
                                                 const char* what) {
        std::vector<AstConst*> conds;
        const std::string kind = choicep->attr("kind");
        if (kind == "choice_by_expression") {
            const Node* const choiceExprp = choicep->child("choice_expression");
            if (!choiceExprp) {
                unsupported(choicep, std::string{what} + " choice without an expression");
                return conds;
            }
            AstNodeExpr* const choiceValp = lowerExpr(choiceExprp, dtypep);
            if (!choiceValp) return conds;
            AstConst* const constp = VN_CAST(choiceValp, Const);
            if (!constp) {
                VL_DO_DANGLING(choiceValp->deleteTree(), choiceValp);
                unsupported(choicep, "non-constant " + std::string{what} + " choice");
                return conds;
            }
            conds.push_back(constp);
            return conds;
        }
        if (kind == "choice_by_range") {
            const Node* const rangep = choicep->child("choice_range");
            int64_t left = 0;
            int64_t right = 0;
            if (!rangep || !evalConst(rangeBound(rangep, true), left)
                || !evalConst(rangeBound(rangep, false), right)) {
                unsupported(choicep, "non-static " + std::string{what} + " range");
                return conds;
            }
            const int64_t lo = std::min(left, right);
            const int64_t hi = std::max(left, right);
            if (hi - lo > 4096) {
                unsupported(choicep, "large " + std::string{what} + " range");
                return conds;
            }
            for (int64_t value = lo; value <= hi; ++value) {
                conds.push_back(makeConst(flpOf(choicep), dtypep, static_cast<uint64_t>(value)));
            }
            return conds;
        }
        unsupported(choicep, std::string{what} + " choice form '" + kind + "'");
        return conds;
    }

    AstNode* lowerSelectedAssignment(const Node* stmtp, AssignMode mode) {
        FileLine* const flp = flpOf(stmtp);
        const Node* const targetp = targetNode(stmtp);
        AstNodeExpr* const selectorp = lowerExpr(stmtp->child("expression"), nullptr);
        if (!selectorp) return nullptr;

        const auto lowerBody = [&](const Node* choicep) -> AstNode* {
            AstNode* bodyp = nullptr;
            for (const Node* const wavep : childEls(choicep->child("associated_chain"))) {
                if (wavep->attr("kind") != "waveform_element") {
                    unsupported(wavep, "selected association form '" + wavep->attr("kind") + "'");
                    continue;
                }
                const Node* const wep = wavep->child("we_value");
                if (!wep) {
                    unsupported(wavep, "selected association without a value");
                    continue;
                }
                AstNodeExpr* const lhsp = lowerLValue(stmtp, targetp);
                AstNodeExpr* const rhsp = lowerExpr(wep, lhsp ? lhsp->dtypep() : nullptr);
                if (!lhsp || !rhsp) continue;
                AstNode* assp = nullptr;
                if (mode == AssignMode::DELAYED) {
                    assp = new AstAssignDly{flp, lhsp, rhsp};
                } else if (mode == AssignMode::BLOCKING) {
                    assp = new AstAssign{flp, lhsp, rhsp};
                } else {
                    assp = new AstAssignW{flp, lhsp, rhsp};
                }
                bodyp = bodyp ? AstNode::addNext<AstNode, AstNode>(bodyp, assp) : assp;
            }
            return bodyp;
        };

        AstCaseItem* itemsp = nullptr;
        AstCaseItem* currentp = nullptr;
        for (const Node* const choicep : childEls(stmtp->child("selected_waveform_chain"))) {
            const bool sameAlternative = choicep->attr("same_alternative_flag") == "true";
            const std::string kind = choicep->attr("kind");
            if (kind == "choice_by_others") {
                currentp = new AstCaseItem{flp, nullptr, lowerBody(choicep)};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
                continue;
            }
            std::vector<AstConst*> conds
                = lowerChoiceConditions(choicep, selectorp->dtypep(), "selected");
            if (conds.empty()) continue;
            if (!sameAlternative || !currentp) {
                AstConst* const firstp = conds.front();
                conds.erase(conds.begin());
                currentp = new AstCaseItem{flp, firstp, lowerBody(choicep)};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
            }
            for (AstConst* const condp : conds) currentp->addCondsp(condp);
        }
        return new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
    }

    AstNode* lowerCase(const Node* stmtp, AssignMode mode) {
        FileLine* const flp = flpOf(stmtp);
        AstNodeExpr* const selectorp = lowerExpr(stmtp->child("expression"), nullptr);
        if (!selectorp) return nullptr;

        AstCaseItem* itemsp = nullptr;
        AstCaseItem* currentp = nullptr;
        for (const Node* const choicep :
             childEls(stmtp->child("case_statement_alternative_chain"))) {
            const bool sameAlternative = choicep->attr("same_alternative_flag") == "true";
            const std::string kind = choicep->attr("kind");
            if (kind == "choice_by_others") {
                currentp = new AstCaseItem{
                    flp, nullptr, lowerStmtChain(choicep->child("associated_chain"), mode)};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
                continue;
            }
            // AstCaseItem conditions are constant values; V3Case builds the comparisons
            std::vector<AstConst*> conds
                = lowerChoiceConditions(choicep, selectorp->dtypep(), "case");
            if (conds.empty()) continue;
            if (!sameAlternative || !currentp) {
                AstConst* const firstp = conds.front();
                conds.erase(conds.begin());
                currentp = new AstCaseItem{
                    flp, firstp, lowerStmtChain(choicep->child("associated_chain"), mode)};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
            }
            for (AstConst* const condp : conds) currentp->addCondsp(condp);
        }
        return new AstCase{flp, VCaseType::CT_CASE, selectorp, itemsp};
    }

    //! Value of a static array attribute ('left/'right/'high/'low/'length/'ascending)
    bool attrValue(const Node* nodep, int64_t& value) const {
        std::string kind = nodep->attr("kind");
        const std::string suffix = "_array_attribute";
        if (kind.size() > suffix.size()
            && kind.compare(kind.size() - suffix.size(), suffix.size(), suffix) == 0) {
            kind.resize(kind.size() - suffix.size());
        }
        if (kind != "left" && kind != "right" && kind != "high" && kind != "low"
            && kind != "length" && kind != "ascending") {
            return false;
        }
        const Node* const prefixp = nodep->child("prefix");
        if (!prefixp || prefixp->attr("kind") != "simple_name") return false;
        AstVar* const varp = lookupVar(prefixp);
        const AstBasicDType* basicp = nullptr;
        if (varp) {
            basicp = VN_CAST(varp->dtypep()->skipRefp(), BasicDType);
        } else if (m_inlineEnvp) {
            const auto it = m_inlineEnvp->find(vhdlFold(prefixp->attr("identifier")));
            if (it != m_inlineEnvp->end() && it->second.exprp) {
                basicp = VN_CAST(it->second.exprp->dtypep()->skipRefp(), BasicDType);
            }
        }
        if (!varp && !basicp) return false;
        // An array of multi-bit elements stores only its bit width in the dtype
        VhdlDTypeSpec layout;
        if (multiArraySpec(prefixp, layout)) {
            const int left = layout.left;
            const int right = layout.right;
            if (kind == "left") {
                value = left;
            } else if (kind == "right") {
                value = right;
            } else if (kind == "high") {
                value = std::max(left, right);
            } else if (kind == "low") {
                value = std::min(left, right);
            } else if (kind == "length") {
                value = rangeLength(left, right, layout.ascending);
            } else {
                value = layout.ascending ? 1 : 0;
            }
            return true;
        }
        if (!basicp) return false;
        const int left = basicp->left();
        const int right = basicp->right();
        if (kind == "left") {
            value = left;
        } else if (kind == "right") {
            value = right;
        } else if (kind == "high") {
            value = std::max(left, right);
        } else if (kind == "low") {
            value = std::min(left, right);
        } else if (kind == "length") {
            value = basicp->width();
        } else {
            value = basicp->ascending() ? 1 : 0;
        }
        return true;
    }

    //! Evaluate a locally static integer expression from the dump
    bool evalConst(const Node* nodep, int64_t& value, int depth = 0) const {
        if (!nodep || depth > 16) return false;
        const std::string kind = nodep->attr("kind");
        if (kind == "integer_literal") {
            value = std::strtoll(nodep->attr("value").c_str(), nullptr, 10);
            return true;
        }
        if (kind == "enumeration_literal") {
            value = std::strtoll(nodep->attr("enum_pos").c_str(), nullptr, 10);
            return true;
        }
        if (kind == "pos_attribute" || kind == "succ_attribute" || kind == "pred_attribute"
            || kind == "val_attribute") {
            int64_t operandv = 0;
            if (!evalConst(nodep->child("parameter"), operandv, depth + 1)) return false;
            if (kind == "succ_attribute") {
                value = operandv + 1;
            } else if (kind == "pred_attribute") {
                value = operandv - 1;
            } else {
                value = operandv;
            }
            return true;
        }
        // GHDL spells the IIR kind "substraction_operator"
        if (kind == "addition_operator" || kind == "subtraction_operator"
            || kind == "substraction_operator" || kind == "multiplication_operator") {
            int64_t lhsv = 0;
            int64_t rhsv = 0;
            if (!evalConst(nodep->child("left"), lhsv, depth + 1)
                || !evalConst(nodep->child("right"), rhsv, depth + 1)) {
                return false;
            }
            if (kind == "addition_operator") {
                value = lhsv + rhsv;
            } else if (kind == "multiplication_operator") {
                value = lhsv * rhsv;
            } else {
                value = lhsv - rhsv;
            }
            return true;
        }
        if (kind == "negation_operator") {
            int64_t operandv = 0;
            if (!evalConst(nodep->child("operand"), operandv, depth + 1)) return false;
            value = -operandv;
            return true;
        }
        if (kind == "parentheses_expression") {
            return evalConst(nodep->child("expression"), value, depth + 1);
        }
        if (kind == "simple_name") {
            const auto it = m_constBindings.find(vhdlFold(nodep->attr("identifier")));
            if (it != m_constBindings.end()) {
                value = it->second;
                return true;
            }
            // A subprogram formal bound to a constant while inlining
            if (m_inlineEnvp) {
                const auto bindIt = m_inlineEnvp->find(vhdlFold(nodep->attr("identifier")));
                if (bindIt != m_inlineEnvp->end() && bindIt->second.exprp) {
                    if (AstConst* const constp = VN_CAST(bindIt->second.exprp, Const)) {
                        value = constp->num().toSInt();
                        return true;
                    }
                }
            }
            // Constants and generic defaults resolve through the analyzed reference
            const Node* const refp = nodep->child("named_entity");
            if (refp && refp->has("ref")) {
                const Node* const declp = nodeById(refp->attr("ref"));
                if (declp) {
                    const std::string declKind = declp->attr("kind");
                    if (declKind == "constant_declaration"
                        || declKind == "interface_constant_declaration") {
                        if (const Node* const defp = declp->child("default_value")) {
                            return evalConst(defp, value, depth + 1);
                        }
                    } else if (declKind == "enumeration_literal") {
                        value = std::strtoll(declp->attr("enum_pos").c_str(), nullptr, 10);
                        return true;
                    } else if (declKind == "object_alias_declaration") {
                        return evalConst(declp->child("name"), value, depth + 1);
                    }
                }
            }
            return false;
        }
        return attrValue(nodep, value);
    }

    //! Evaluate a locally static boolean expression (if-generate conditions)
    bool evalBool(const Node* nodep, bool& value) const {
        if (!nodep) return false;
        const std::string kind = nodep->attr("kind");
        if (kind == "simple_name" || kind == "enumeration_literal") {
            const std::string id = vhdlFold(nodep->attr("identifier"));
            if (id == "true" || id == "false") {
                value = id == "true";
                return true;
            }
            if (kind != "simple_name") return false;
            // Constants and generic defaults resolve through the analyzed reference
            const Node* const refp = nodep->child("named_entity");
            if (!refp || !refp->has("ref")) return false;
            const Node* const declp = nodeById(refp->attr("ref"));
            if (!declp) return false;
            const std::string declKind = declp->attr("kind");
            if (declKind == "enumeration_literal") return evalBool(declp, value);
            if ((declKind == "constant_declaration"
                 || declKind == "interface_constant_declaration")
                && declp->child("default_value")) {
                return evalBool(declp->child("default_value"), value);
            }
            return false;
        }
        if (kind == "parentheses_expression" || kind == "parenthesis_expression") {
            return evalBool(nodep->child("expression"), value);
        }
        if (kind == "and_operator" || kind == "or_operator" || kind == "xor_operator") {
            bool lhsv = false;
            bool rhsv = false;
            if (!evalBool(nodep->child("left"), lhsv) || !evalBool(nodep->child("right"), rhsv)) {
                return false;
            }
            if (kind == "and_operator") {
                value = lhsv && rhsv;
            } else if (kind == "or_operator") {
                value = lhsv || rhsv;
            } else {
                value = lhsv != rhsv;
            }
            return true;
        }
        if (kind == "not_operator") {
            bool operandv = false;
            if (!evalBool(nodep->child("operand"), operandv)) return false;
            value = !operandv;
            return true;
        }
        if (kind == "equality_operator" || kind == "inequality_operator"
            || kind == "less_than_operator" || kind == "less_than_or_equal_operator"
            || kind == "greater_than_operator" || kind == "greater_than_or_equal_operator") {
            int64_t lhsv = 0;
            int64_t rhsv = 0;
            if (!evalConst(nodep->child("left"), lhsv)
                || !evalConst(nodep->child("right"), rhsv)) {
                return false;
            }
            if (kind == "equality_operator") {
                value = lhsv == rhsv;
            } else if (kind == "inequality_operator") {
                value = lhsv != rhsv;
            } else if (kind == "less_than_operator") {
                value = lhsv < rhsv;
            } else if (kind == "less_than_or_equal_operator") {
                value = lhsv <= rhsv;
            } else if (kind == "greater_than_operator") {
                value = lhsv > rhsv;
            } else {
                value = lhsv >= rhsv;
            }
            return true;
        }
        return false;
    }

    //! Resolve one bound of a discrete range; limits may be references into the dump
    const Node* rangeBound(const Node* rangep, bool leftBound) const {
        rangep = derefNode(rangep);
        if (!rangep) return nullptr;
        const Node* boundp = rangep->child(leftBound ? "left_limit_expr" : "right_limit_expr");
        if (boundp) return boundp;
        boundp = rangep->child(leftBound ? "left_limit" : "right_limit");
        if (!boundp) return nullptr;
        if (boundp->has("ref")) return nodeById(boundp->attr("ref"));
        return boundp->has("kind") ? boundp : nullptr;
    }

    //! Evaluate a discrete range to integer bounds and direction
    bool discreteRange(const Node* rangep, int& left, int& right, bool& ascending) const {
        rangep = derefNode(rangep);
        if (!rangep) return false;
        ascending = rangep->attr("direction") == "to";
        int64_t leftValue = 0;
        int64_t rightValue = 0;
        if (evalConst(rangeBound(rangep, true), leftValue)
            && evalConst(rangeBound(rangep, false), rightValue)) {
            left = static_cast<int>(leftValue);
            right = static_cast<int>(rightValue);
            return true;
        }
        // "for i in a'range" carries the array's range as an attribute origin
        const Node* const originp = rangep->child("range_origin");
        if (originp && originp->attr("kind") == "range_array_attribute") {
            const Node* const prefixp = originp->child("prefix");
            if (!prefixp) return false;
            // An array of multi-bit elements stores only its bit width in the dtype
            VhdlDTypeSpec layout;
            if (multiArraySpec(prefixp, layout)) {
                left = layout.left;
                right = layout.right;
                ascending = layout.ascending;
                return true;
            }
            const auto it = m_scope.find(vhdlFold(prefixp->attr("identifier")));
            if (it == m_scope.end()) return false;
            const AstBasicDType* const basicp
                = VN_CAST(it->second->dtypep()->skipRefp(), BasicDType);
            if (!basicp || basicp->width() <= 1) return false;
            left = basicp->left();
            right = basicp->right();
            ascending = basicp->ascending();
            return true;
        }
        return false;
    }

    AstNode* lowerForLoop(const Node* stmtp, AssignMode mode) {
        const Node* const specp = stmtp->child("parameter_specification");
        if (!specp || specp->attr("kind") != "iterator_declaration") {
            unsupported(stmtp, "for loop without an iterator specification");
            return nullptr;
        }
        const std::string name = specp->attr("identifier");
        const Node* const subtypep = specp->child("subtype_indication");
        const Node* const rangep = subtypep ? subtypep->child("range_constraint") : nullptr;
        int left = 0;
        int right = 0;
        bool ascending = true;
        if (!discreteRange(rangep, left, right, ascending)) {
            unsupported(stmtp, "non-static loop range");
            return nullptr;
        }

        const std::string key = vhdlFold(name);
        // A loop parameter shadows a formal parameter or local of the same name
        InlineBinding savedBinding;
        bool hadInlineBinding = false;
        if (m_inlineEnvp) {
            const auto it = m_inlineEnvp->find(key);
            if (it != m_inlineEnvp->end()) {
                hadInlineBinding = true;
                savedBinding = it->second;
                m_inlineEnvp->erase(it);
            }
        }
        const int step = ascending ? 1 : -1;
        AstNode* headp = nullptr;
        for (int value = left; ascending ? value <= right : value >= right; value += step) {
            const auto saved = m_constBindings.find(key);
            const bool hadBinding = saved != m_constBindings.end();
            const int64_t savedValue = hadBinding ? saved->second : 0;
            m_constBindings[key] = value;
            AstNode* const bodyp
                = lowerStmtChain(stmtp->child("sequential_statement_chain"), mode);
            if (hadBinding) {
                m_constBindings[key] = savedValue;
            } else {
                m_constBindings.erase(key);
            }
            if (!bodyp) continue;
            headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, bodyp) : bodyp;
        }
        if (hadInlineBinding && m_inlineEnvp) (*m_inlineEnvp)[key] = savedBinding;
        return headp;
    }

    AstNode* lowerStmt(const Node* stmtp, AssignMode mode) {
        const std::string kind = stmtp->attr("kind");
        if (kind == "simple_signal_assignment_statement") { return lowerAssignment(stmtp, mode); }
        if (kind == "variable_assignment_statement") return lowerVariableAssignment(stmtp);
        if (kind == "procedure_call_statement") return lowerProcedureCall(stmtp, mode);
        if (kind == "case_statement") return lowerCase(stmtp, mode);
        if (kind == "for_loop_statement") return lowerForLoop(stmtp, mode);
        if (kind == "selected_waveform_assignment_statement") {
            return lowerSelectedAssignment(stmtp, mode);
        }
        // Assertions and reports are simulation diagnostics; synthesis (UG901) ignores them
        if (kind == "null_statement" || kind == "assertion_statement"
            || kind == "report_statement") {
            return nullptr;
        }
        if (kind == "if_statement") {
            FileLine* const flp = flpOf(stmtp);
            AstNodeExpr* const condp = lowerExpr(stmtp->child("condition"), bitDType(flp));
            AstNode* const thensp
                = lowerStmtChain(stmtp->child("sequential_statement_chain"), mode);
            AstNode* const elsesp = stmtp->child("else_clause")
                                        ? lowerElse(stmtp->child("else_clause"), mode)
                                        : nullptr;
            return new AstIf{flp, condp, thensp, elsesp};
        }
        unsupported(stmtp, "statement '" + kind + "'");
        return nullptr;
    }

    AstNode* lowerStmtChain(const Node* chainp, AssignMode mode) {
        AstNode* headp = nullptr;
        for (const Node* const stmtp : childEls(chainp)) {
            AstNode* const nodep = lowerStmt(stmtp, mode);
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
        // A conditional concurrent assignment gated by a clock edge is a register
        if (emitClockedIf(processp, true)) return;
        AstNode* headp = nullptr;
        for (const Node* const stmtp : childEls(processp->child("sequential_statement_chain"))) {
            const std::string kind = stmtp->attr("kind");
            // A concurrent procedure call is wrapped in a process with a generated wait;
            // the AstAlways sensitivity below supplies the equivalent trigger
            if (kind == "suspend_state_statement" || kind == "wait_statement") continue;
            AstNode* const nodep = lowerStmt(stmtp, AssignMode::CONT);
            if (!nodep) continue;
            headp = headp ? AstNode::addNext<AstNode, AstNode>(headp, nodep) : nodep;
        }
        if (!headp) return;
        m_modp->addStmtsp(new AstAlways{flpOf(processp), VAlwaysKwd::CONT_ASSIGN, nullptr, headp});
    }

    static AstVar* astVarOf(AstNodeExpr* exprp) {
        AstNodeVarRef* const refp = VN_CAST(exprp, NodeVarRef);
        return refp ? refp->varp() : nullptr;
    }

    bool sensitivityHasVar(const Node* sensp, const AstVar* varp) const {
        if (!sensp) return false;
        for (const Node* const elp : childEls(sensp)) {
            const Node* const namep = elp->attr("ref").empty() ? elp : nodeById(elp->attr("ref"));
            if (!namep || namep->attr("kind") != "simple_name") continue;
            const auto it = m_scope.find(vhdlFold(namep->attr("identifier")));
            if (it != m_scope.end() && it->second == varp) return true;
        }
        return false;
    }

    //! Edge information extracted from a rising_edge/falling_edge call
    struct EdgeInfo final {
        VEdgeType edgeType = VEdgeType::ET_CHANGED;
        AstNodeExpr* sigp = nullptr;
    };

    bool edgeCall(const Node* condp, EdgeInfo& info) {
        if (!condp || condp->attr("kind") != "function_call") return false;
        const Node* const prefixp = condp->child("prefix");
        const std::string fn = prefixp ? vhdlFold(prefixp->attr("identifier")) : std::string{};
        if (fn != "rising_edge" && fn != "falling_edge") return false;
        AstNodeExpr* const sigp = singleActual(condp);
        if (!sigp) return false;
        info.edgeType = fn == "rising_edge" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
        info.sigp = sigp;
        return true;
    }

    //! Flatten `and` and parenthesized conditions into their conjuncts
    static void flattenConjuncts(const Node* nodep, std::vector<const Node*>& out) {
        if (!nodep) return;
        const std::string kind = nodep->attr("kind");
        if (kind == "and_operator") {
            flattenConjuncts(nodep->child("left"), out);
            flattenConjuncts(nodep->child("right"), out);
        } else if (kind == "parentheses_expression" || kind == "parenthesis_expression") {
            flattenConjuncts(nodep->child("expression"), out);
        } else {
            out.push_back(nodep);
        }
    }

    static bool isSignalName(const Node* nodep, const std::string& folded) {
        return nodep && nodep->attr("kind") == "simple_name"
               && vhdlFold(nodep->attr("identifier")) == folded;
    }

    //! Static '0'/'1' value of a std_logic expression, or 0 when not known
    char staticLogic(const Node* nodep, int depth = 0) const {
        if (!nodep || depth > 16) return 0;
        const std::string kind = nodep->attr("kind");
        if (kind == "character_literal" || kind == "enumeration_literal") {
            const std::string id = nodep->attr("identifier");
            if (id == "'0'" || id == "0") return '0';
            if (id == "'1'" || id == "1") return '1';
            return 0;
        }
        if (kind == "parentheses_expression" || kind == "parenthesis_expression") {
            return staticLogic(nodep->child("expression"), depth + 1);
        }
        if (kind != "simple_name") return 0;
        const Node* const refp = nodep->child("named_entity");
        if (!refp || !refp->has("ref")) return 0;
        const Node* const declp = nodeById(refp->attr("ref"));
        if (!declp) return 0;
        const std::string declKind = declp->attr("kind");
        if (declKind == "enumeration_literal") return staticLogic(declp, depth + 1);
        if ((declKind == "constant_declaration" || declKind == "interface_constant_declaration")
            && declp->child("default_value")) {
            return staticLogic(declp->child("default_value"), depth + 1);
        }
        return 0;
    }

    //! Decompose a condition into a clock edge and the remaining level conjuncts
    bool splitClockCondition(const Node* condp, EdgeInfo& info, std::vector<const Node*>& levelp) {
        levelp.clear();
        std::vector<const Node*> parts;
        flattenConjuncts(condp, parts);
        int edgeCount = 0;
        std::vector<size_t> events;
        for (const Node* const partp : parts) {
            EdgeInfo edge;
            if (edgeCall(partp, edge)) {
                info = edge;
                ++edgeCount;
            } else if (partp->attr("kind") == "event_attribute") {
                events.push_back(levelp.size());
                levelp.push_back(partp);
            } else {
                levelp.push_back(partp);
            }
        }
        if (edgeCount > 1) return false;
        if (edgeCount == 1) return true;
        if (events.size() != 1) return false;
        // "sig'event and sig = '1'": the comparison supplies the edge polarity
        const Node* const eventp = levelp[events.front()];
        const Node* const prefixp = eventp->child("prefix");
        if (!prefixp || prefixp->attr("kind") != "simple_name") return false;
        const std::string sigName = vhdlFold(prefixp->attr("identifier"));
        for (size_t i = 0; i < levelp.size(); ++i) {
            if (i == events.front()) continue;
            const Node* const cmpp = levelp[i];
            const std::string cmpKind = cmpp->attr("kind");
            if (cmpKind != "equality_operator" && cmpKind != "inequality_operator") continue;
            const Node* const lhsp = cmpp->child("left");
            const Node* const rhsp = cmpp->child("right");
            const Node* namep = nullptr;
            const Node* valuep = nullptr;
            if (isSignalName(lhsp, sigName)) {
                namep = lhsp;
                valuep = rhsp;
            } else if (isSignalName(rhsp, sigName)) {
                namep = rhsp;
                valuep = lhsp;
            }
            char value = namep ? staticLogic(valuep) : 0;
            if (!value) continue;
            if (cmpKind == "inequality_operator") value = value == '1' ? '0' : '1';
            info.edgeType = value == '1' ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
            info.sigp = lowerExpr(namep, nullptr);
            if (!info.sigp) return false;
            std::vector<const Node*> rest;
            for (size_t j = 0; j < levelp.size(); ++j) {
                if (j != i && j != events.front()) rest.push_back(levelp[j]);
            }
            levelp.swap(rest);
            return true;
        }
        return false;
    }

    //! One branch of a clocked if/elsif chain
    struct ClockBranch final {
        const Node* nodep = nullptr;  // if_statement or elsif
        bool clocked = false;  // This branch's condition holds the clock edge
        EdgeInfo edge;  // Clock edge when clocked
        std::vector<const Node*> levels;  // Remaining level conjuncts when clocked
    };

    enum class ClockChainStatus : uint8_t { NOT_CLOCKED, CLOCKED, ELSE_UNSUPPORTED };

    //! Find the single branch of an if/elsif chain that holds the clock edge
    ClockChainStatus analyzeClockChain(const Node* ifp, std::vector<ClockBranch>& branches,
                                       int& clockIndex) {
        branches.clear();
        clockIndex = -1;
        const Node* branchp = ifp;
        while (branchp) {
            ClockBranch branch;
            branch.nodep = branchp;
            std::vector<const Node*> levels;
            EdgeInfo edge;
            if (splitClockCondition(branchp->child("condition"), edge, levels)) {
                branch.clocked = true;
                branch.edge = edge;
                branch.levels = levels;
                if (clockIndex >= 0) return ClockChainStatus::NOT_CLOCKED;  // Multiple edges
                clockIndex = static_cast<int>(branches.size());
            }
            branches.push_back(branch);
            const Node* const elsep = branchp->child("else_clause");
            if (!elsep) break;
            if (elsep->attr("kind") != "elsif") {
                return clockIndex >= 0 ? ClockChainStatus::ELSE_UNSUPPORTED
                                       : ClockChainStatus::NOT_CLOCKED;
            }
            branchp = elsep;
        }
        return clockIndex >= 0 ? ClockChainStatus::CLOCKED : ClockChainStatus::NOT_CLOCKED;
    }

    //! Recognize a simple level reset condition; does not lower or report diagnostics
    bool levelResetEdge(const Node* condp, AstVar*& varp, VEdgeType& edgeType) const {
        if (!condp) return false;
        const std::string kind = condp->attr("kind");
        const Node* namep = nullptr;
        if (kind == "simple_name") {
            namep = condp;
            edgeType = VEdgeType::ET_POSEDGE;
        } else if (kind == "not_operator") {
            namep = condp->child("operand");
            edgeType = VEdgeType::ET_NEGEDGE;
        } else if (kind == "equality_operator") {
            const Node* const lhsp = condp->child("left");
            const Node* const rhsp = condp->child("right");
            const Node* valuep = nullptr;
            if (lhsp && lhsp->attr("kind") == "simple_name") {
                namep = lhsp;
                valuep = rhsp;
            } else if (rhsp && rhsp->attr("kind") == "simple_name") {
                namep = rhsp;
                valuep = lhsp;
            }
            const char value = valuep ? staticLogic(valuep) : 0;
            if (!value) return false;
            edgeType = value == '1' ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
        }
        if (!namep || namep->attr("kind") != "simple_name") return false;
        AstVar* const foundp = lookupVar(namep);
        if (!foundp) return false;
        varp = foundp;
        return true;
    }

    //! Whether a condition references a signal from the sensitivity list
    bool conditionUsesSensitivity(const Node* nodep, const Node* sensp,
                                  const AstVar* loopp) const {
        if (!nodep) return false;
        if (nodep->attr("kind") == "simple_name") {
            AstVar* const varp = lookupVar(nodep);
            if (varp && varp != loopp && sensitivityHasVar(sensp, varp)) return true;
        }
        for (const std::unique_ptr<Node>& childp : nodep->children()) {
            if (conditionUsesSensitivity(childp.get(), sensp, loopp)) return true;
        }
        return false;
    }

    //! Lower the non-edge part of a clocked branch condition; nullptr means "true"
    AstNodeExpr* lowerLevelCondition(FileLine* flp, const std::vector<const Node*>& levelsp) {
        AstNodeExpr* resultp = nullptr;
        for (const Node* const levelp : levelsp) {
            AstNodeExpr* const partp = lowerExpr(levelp, bitDType(flpOf(levelp)));
            if (!partp) return nullptr;
            if (!resultp) {
                resultp = partp;
            } else {
                AstNodeExpr* const andp = new AstAnd{flp, resultp, partp};
                andp->dtypep(bitDType(flp));
                resultp = andp;
            }
        }
        return resultp;
    }

    //! Lower an if/elsif chain; the clocked branch's edge is true on the clock and false
    //! at time zero, when the edge function cannot see a previous value
    AstNode* buildClockChain(FileLine* flp, const std::vector<ClockBranch>& branches,
                             int clockIndex, AssignMode mode, bool atTimeZero) {
        AstNode* resultp = nullptr;
        for (size_t i = branches.size(); i-- > 0;) {
            const ClockBranch& branch = branches[i];
            const Node* const chainp = branch.nodep->child("sequential_statement_chain");
            AstNodeExpr* condp = nullptr;
            if (branch.clocked) {
                if (!atTimeZero) condp = lowerLevelCondition(flp, branch.levels);
                if (!condp) { condp = makeConst(flp, bitDType(flp), atTimeZero ? 0U : 1U); }
            } else {
                condp = lowerExpr(branch.nodep->child("condition"), bitDType(flp));
                if (!condp) return nullptr;
            }
            AstNode* const stmtsp = lowerStmtChain(chainp, mode);
            resultp = new AstIf{flp, condp, stmtsp, resultp};
        }
        return resultp;
    }

    //! Emit a clocked process when a single top-level if/elsif chain holds exactly one
    //! rising_edge/falling_edge/'event condition.  Returns false when not recognizable.
    bool emitClockedIf(const Node* processp, bool concurrent) {
        const Node* const ifp = singleTopIf(processp->child("sequential_statement_chain"));
        if (!ifp) return false;
        std::vector<ClockBranch> branches;
        int clockIndex = -1;
        const ClockChainStatus status = analyzeClockChain(ifp, branches, clockIndex);
        if (status == ClockChainStatus::ELSE_UNSUPPORTED) {
            unsupported(ifp, "else branch in a clocked process");
            return true;
        }
        if (status != ClockChainStatus::CLOCKED) return false;
        if (clockIndex != static_cast<int>(branches.size()) - 1) {
            // A later branch would also run on non-clock wakeups in VHDL
            unsupported(ifp, "clock edge before a later branch");
            return true;
        }
        FileLine* const flp = flpOf(ifp);
        EdgeInfo& clock = branches[clockIndex].edge;
        AstVar* const clockVarp = astVarOf(clock.sigp);
        AstSenItem* itemsp = new AstSenItem{flp, clock.edgeType, clock.sigp};
        const Node* const sensp = processp->child("sensitivity_list");
        for (int i = 0; i < clockIndex; ++i) {
            const Node* const condp = branches[i].nodep->child("condition");
            AstVar* rstVarp = nullptr;
            VEdgeType rstEdge = VEdgeType::ET_CHANGED;
            if (levelResetEdge(condp, rstVarp, rstEdge)) {
                if (!concurrent && !sensitivityHasVar(sensp, rstVarp)) continue;
                AstNodeExpr* const rstRefp = new AstVarRef{flp, rstVarp, VAccess::READ};
                AstNode::addNext<AstSenItem, AstSenItem>(itemsp,
                                                         new AstSenItem{flp, rstEdge, rstRefp});
            } else if (conditionUsesSensitivity(condp, sensp, clockVarp)) {
                // A level condition on another sensitivity-list signal is an asynchronous
                // reset we cannot model
                unsupported(condp, "asynchronous reset condition");
                return true;
            }
        }
        AstNode* const stmtsp
            = buildClockChain(flp, branches, clockIndex, AssignMode::DELAYED, false);
        if (!stmtsp) return false;
        m_modp->addStmtsp(
            new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemsp}, stmtsp});
        // VHDL executes the process once at time zero, before any clock edge
        if (AstNode* const initp
            = buildClockChain(flp, branches, clockIndex, AssignMode::BLOCKING, true)) {
            m_modp->addStmtsp(new AstInitialStatic{flp, initp});
        }
        return true;
    }

    //! Declarations of a process: variables live in a process-local scope
    void emitProcessVariables(const Node* processp) {
        const Node* const declsp = processp->child("declaration_chain");
        if (!declsp) return;
        for (const Node* const declp : childEls(declsp)) {
            if (ignorableDeclaration(declp->attr("kind"))) continue;
            if (declp->attr("kind") != "variable_declaration") {
                unsupported(declp, "process declaration '" + declp->attr("kind") + "'");
                continue;
            }
            const Node* const subtypep = declp->child("subtype_indication");
            if (!subtypep) {
                error(declp, "VHDL variable without a subtype");
                continue;
            }
            FileLine* const dflp = flpOf(declp);
            const std::string name = declp->attr("identifier");
            // Variables are process-local; mangle to keep same-named variables of different
            // processes apart in the flat module namespace
            const std::string mangled = "vhdlvar_" + std::to_string(++m_varSeq) + "__" + name;
            AstVar* const varp
                = new AstVar{dflp, VVarType::VAR, mangled, dtypeOf(parseType(subtypep), dflp)};
            m_modp->addStmtsp(varp);
            m_scope[vhdlFold(name)] = varp;
            if (const Node* const defp = declp->child("default_value")) {
                AstNodeExpr* const rhsp = lowerExpr(defp, varp->dtypep());
                if (rhsp) {
                    AstVarRef* const lhsp = new AstVarRef{dflp, varp, VAccess::WRITE};
                    m_modp->addStmtsp(new AstInitialStatic{dflp, new AstAssign{dflp, lhsp, rhsp}});
                }
            }
        }
    }

    void emitProcess(const Node* processp) {
        FileLine* const flp = flpOf(processp);
        VL_RESTORER_COPY(m_scope);
        const Node* const bodyp = processp->child("sequential_statement_chain");
        emitProcessVariables(processp);

        // Clocked process: a single top-level if/elsif chain holding a clock edge
        if (emitClockedIf(processp, false)) return;

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
                // VHDL processes execute once at time zero, then on each sensitivity event
                AstSenItem* const itemp = new AstSenItem{flp, VEdgeType::ET_CHANGED, exprp};
                itemsp = itemsp ? AstNode::addNext<AstSenItem, AstSenItem>(itemsp, itemp) : itemp;
            }
        }
        if (!itemsp) {
            unsupported(processp, "process without a sensitivity list");
            return;
        }
        AstNode* const stmtsp = lowerStmtChain(bodyp, AssignMode::DELAYED);
        if (!stmtsp) return;
        // VHDL executes a process once at time zero, before any sensitivity event. Blocking
        // assignments are used there: no other process observes the interim delta.
        AstNode* const initp = lowerStmtChain(bodyp, AssignMode::BLOCKING);
        if (initp) m_modp->addStmtsp(new AstInitialStatic{flp, initp});
        m_modp->addStmtsp(
            new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemsp}, stmtsp});
    }

    void emitGenerateBody(const Node* bodyp, const std::string& suffix) {
        VL_RESTORER_COPY(m_scope);
        const std::string savedSuffix = m_genSuffix;
        m_genSuffix = suffix;
        emitGenerateDeclarations(bodyp);
        if (const Node* const concp = bodyp->child("concurrent_statement_chain")) {
            for (const Node* const stmtp2 : childEls(concp)) lowerConcurrent(stmtp2);
        }
        m_genSuffix = savedSuffix;
    }

    //! One else/elsif branch of an if-generate; recurses through the else chain
    void lowerGenerateElse(const Node* elseClausep, const std::string& suffix) {
        if (!elseClausep) return;
        bool take = true;
        if (const Node* const condp = elseClausep->child("condition")) {
            if (!evalBool(condp, take)) {
                unsupported(elseClausep, "non-static if-generate condition");
                return;
            }
        }
        if (take) {
            if (const Node* const bodyp = elseClausep->child("generate_statement_body")) {
                emitGenerateBody(bodyp, suffix);
            } else {
                unsupported(elseClausep, "selected generate branch without a body");
            }
            return;
        }
        lowerGenerateElse(elseClausep->child("generate_else_clause"), suffix);
    }

    void lowerIfGenerate(const Node* stmtp) {
        bool condition = false;
        if (!evalBool(stmtp->child("condition"), condition)) {
            unsupported(stmtp, "non-static if-generate condition");
            return;
        }
        const std::string suffix = m_genSuffix + "__ifgen" + std::to_string(++m_varSeq);
        if (condition) {
            if (const Node* const bodyp = stmtp->child("generate_statement_body")) {
                emitGenerateBody(bodyp, suffix);
            } else {
                unsupported(stmtp, "selected generate branch without a body");
            }
        } else {
            lowerGenerateElse(stmtp->child("generate_else_clause"), suffix);
        }
    }

    void lowerForGenerate(const Node* stmtp) {
        const Node* const specp = stmtp->child("parameter_specification");
        if (!specp || specp->attr("kind") != "iterator_declaration") {
            unsupported(stmtp, "for generate without an iterator specification");
            return;
        }
        const Node* const subtypep = specp->child("subtype_indication");
        const Node* const rangep = subtypep ? subtypep->child("range_constraint") : nullptr;
        int left = 0;
        int right = 0;
        bool ascending = true;
        if (!discreteRange(rangep, left, right, ascending)) {
            unsupported(stmtp, "non-static generate range");
            return;
        }
        const Node* const bodyp = stmtp->child("generate_statement_body");
        if (!bodyp) {
            unsupported(stmtp, "generate without a body");
            return;
        }

        const std::string key = vhdlFold(specp->attr("identifier"));
        const int genSeq = ++m_genSeq;
        const int step = ascending ? 1 : -1;
        for (int value = left; ascending ? value <= right : value >= right; value += step) {
            const auto saved = m_constBindings.find(key);
            const bool hadBinding = saved != m_constBindings.end();
            const int64_t savedValue = hadBinding ? saved->second : 0;
            m_constBindings[key] = value;

            emitGenerateBody(bodyp, m_genSuffix + "__gen" + std::to_string(genSeq) + "_"
                                        + std::to_string(value));

            if (hadBinding) {
                m_constBindings[key] = savedValue;
            } else {
                m_constBindings.erase(key);
            }
        }
    }

    //! Declarations inside one generate iteration; they are flattened with unique names
    void emitGenerateDeclarations(const Node* bodyp) {
        const Node* const declsp = bodyp->child("declaration_chain");
        if (!declsp) return;
        for (const Node* const declp : childEls(declsp)) {
            const std::string kind = declp->attr("kind");
            if (kind == "component_declaration") continue;
            if (ignorableDeclaration(kind)) continue;
            if (kind != "signal_declaration") {
                unsupported(declp, "generate declaration '" + kind + "'");
                continue;
            }
            const Node* const subtypep = declp->child("subtype_indication");
            if (!subtypep) {
                error(declp, "VHDL signal without a subtype");
                continue;
            }
            FileLine* const dflp = flpOf(declp);
            const std::string name = declp->attr("identifier");
            const std::string mangled = "vhdlgen_" + std::to_string(++m_varSeq) + "__" + name;
            AstVar* const varp
                = new AstVar{dflp, VVarType::VAR, mangled, dtypeOf(parseType(subtypep), dflp)};
            m_modp->addStmtsp(varp);
            m_scope[vhdlFold(name)] = varp;
            if (const Node* const defp = declp->child("default_value")) {
                varp->noCReset(true);
                AstNodeExpr* const rhsp = lowerExpr(defp, varp->dtypep());
                if (rhsp) {
                    AstVarRef* const lhsp = new AstVarRef{dflp, varp, VAccess::WRITE};
                    m_modp->addStmtsp(new AstInitialStatic{dflp, new AstAssign{dflp, lhsp, rhsp}});
                }
            }
        }
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
        // A concurrent procedure call is wrapped in a plain process with a process origin
        if (kind == "process_statement" && stmtp->child("process_origin")) {
            emitConcurrentAssignment(stmtp);
            return;
        }
        if (kind == "component_instantiation_statement") {
            lowerInstance(stmtp);
            return;
        }
        if (kind == "for_generate_statement") {
            lowerForGenerate(stmtp);
            return;
        }
        if (kind == "if_generate_statement") {
            lowerIfGenerate(stmtp);
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

    void createModule(const Node* entityp) {
        createModuleFor(entityp, m_modules[vhdlFold(entityp->attr("identifier"))],
                        entityp->attr("identifier"));
    }

    void createModuleFor(const Node* entityp, ModuleInfo& info, const std::string& modName,
                         const std::map<std::string, PortInfo>* portInfos = nullptr) {
        FileLine* const flp = flpOf(entityp);
        info.modp = new AstModule{flp, modName, "work"};
        // One VHDL file may hold many design units; the SV filename convention does not apply
        flp->warnOff(V3ErrorCode::DECLFILENAME, true);
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
                const VhdlDTypeSpec* specp = nullptr;
                const PortInfo* portInfo = nullptr;
                if (portInfos) {
                    const auto infoIt = portInfos->find(vhdlFold(pname));
                    if (infoIt != portInfos->end()) {
                        portInfo = &infoIt->second;
                        specp = &portInfo->spec;
                    }
                }
                const VhdlDTypeSpec spec = specp ? *specp : parseType(subtypep);
                AstVar* const varp = new AstVar{pflp, VVarType::PORT, pname, dtypeOf(spec, pflp)};
                if (portInfo && portInfo->record.dtypep) m_varRecords[varp] = portInfo->record;
                varp->direction(directionOf(portp->attr("mode")));
                varp->pinNum(++pinNum);
                // VHDL ports are signals, so procedural assignment is legal
                varp->declTyped(true);
                info.modp->addStmtsp(varp);
                info.portList.push_back(varp);
                info.ports[vhdlFold(pname)] = varp;
            }
        }
        m_rootp->addModulesp(info.modp);
    }

    void populateModule(const Node* entityp, const Node* archp) {
        populateModuleFor(entityp, archp, m_modules[vhdlFold(entityp->attr("identifier"))]);
    }

    void populateModuleFor(const Node* entityp, const Node* archp, ModuleInfo& info) {
        // Nested specialization re-enters this function; keep the parent's state intact
        VL_RESTORER(m_modp);
        VL_RESTORER_COPY(m_scope);
        VL_RESTORER_COPY(m_moduleScope);
        VL_RESTORER_COPY(m_components);
        m_modp = info.modp;
        m_scope = info.ports;
        m_moduleScope.clear();
        m_components.clear();

        if (const Node* const declsp = archp->child("declaration_chain")) {
            for (const Node* const declp : childEls(declsp)) {
                const std::string kind = declp->attr("kind");
                if (kind == "component_declaration") {
                    // Interface contract checked when the component is instantiated
                    m_components[vhdlFold(declp->attr("identifier"))] = declp;
                    continue;
                }
                if (ignorableDeclaration(kind)) continue;
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
                    AstNodeExpr* const rhsp = lowerExpr(defp, varp->dtypep());
                    if (rhsp) {
                        AstVarRef* const lhsp = new AstVarRef{dflp, varp, VAccess::WRITE};
                        m_modp->addStmtsp(
                            new AstInitialStatic{dflp, new AstAssign{dflp, lhsp, rhsp}});
                    }
                }
            }
        }

        // Subprogram bodies resolve names against the declarative part only
        m_moduleScope = m_scope;

        if (const Node* const stmtsp = archp->child("concurrent_statement_chain")) {
            for (const Node* const stmtp : childEls(stmtsp)) lowerConcurrent(stmtp);
        }
    }

    std::string instantiatedUnitName(const Node* unitp) const {
        std::string name = unitp->attr("identifier");
        if (!name.empty()) return name;
        // Direct entity instantiation: entity work.foo / entity lib.foo
        if (const Node* const entityNamep = unitp->child("entity_name")) {
            name = entityNamep->attr("identifier");
            if (!name.empty()) return name;
            if (const Node* const refp = entityNamep->child("named_entity")) {
                if (const Node* const entityp = nodeById(refp->attr("ref"))) {
                    return entityp->attr("identifier");
                }
            }
        }
        return {};
    }

    //! Check a component declaration's ports against the entity it binds to
    void validateComponent(const Node* componentp, const ModuleInfo& target,
                           const std::string& unitName) {
        const Node* const portsp = componentp->child("port_chain");
        if (!portsp) return;
        for (const Node* const portp : childEls(portsp)) {
            if (portp->attr("kind") != "interface_signal_declaration") continue;
            const std::string name = portp->attr("identifier");
            const auto it = target.ports.find(vhdlFold(name));
            if (it == target.ports.end()) {
                error(portp, "Component '" + unitName + "' port '" + name
                                 + "' has no matching entity port");
                continue;
            }
            AstVar* const varp = it->second;
            if (!(directionOf(portp->attr("mode")) == varp->direction())) {
                error(portp, "Component '" + unitName + "' port '" + name
                                 + "' direction does not match the entity");
            }
            if (const Node* const subtypep = portp->child("subtype_indication")) {
                // An unconstrained component port takes the actual's width
                if (probeTypeSpec(subtypep).isUnconstrained) continue;
                const int width = dtypeOf(parseType(subtypep), flpOf(portp))->width();
                if (width != varp->width()) {
                    error(portp, "Component '" + unitName + "' port '" + name + "' width "
                                     + std::to_string(width) + " does not match the entity ("
                                     + std::to_string(varp->width()) + ")");
                }
            }
        }
    }

    //! Whether an entity has ports whose range comes from the instantiation
    bool hasUnconstrainedPorts(const Node* entityp) const {
        const Node* const portsp = entityp->child("port_chain");
        if (!portsp) return false;
        for (const Node* const portp : childEls(portsp)) {
            if (portp->attr("kind") != "interface_signal_declaration") continue;
            if (probeTypeSpec(portp->child("subtype_indication")).isUnconstrained) return true;
        }
        return false;
    }

    //! Whether an entity has generic type parameters (needing specialization)
    static bool hasTypeGenerics(const Node* entityp) {
        for (const Node* const declp : childEls(entityp->child("generic_chain"))) {
            if (declp->attr("kind") == "interface_type_declaration") return true;
        }
        return false;
    }

    std::vector<GenericDecl> genericsOf(const Node* entityp) const {
        std::vector<GenericDecl> result;
        const Node* const chainp = entityp->child("generic_chain");
        if (!chainp) return result;
        for (const Node* const declp : childEls(chainp)) {
            if (declp->attr("kind") != "interface_constant_declaration") continue;
            GenericDecl decl;
            decl.name = declp->attr("identifier");
            decl.defaultp = declp->child("default_value");
            result.push_back(decl);
        }
        return result;
    }

    //! Emit (or reuse) a module specialized for one generic binding set
    ModuleInfo* specializeModule(const Node* entityp,
                                 const std::map<std::string, int64_t>& bindings,
                                 const std::map<std::string, const Node*>& typeBindings,
                                 const std::map<std::string, PortInfo>& portInfos = {}) {
        const std::string entityKey = vhdlFold(entityp->attr("identifier"));
        std::string key = entityKey;
        for (const auto& binding : bindings) {
            key += "|" + binding.first + "=" + std::to_string(binding.second);
        }
        for (const auto& binding : typeBindings) {
            key += "|" + binding.first + "@" + binding.second->attr("id");
        }
        for (const auto& portInfo : portInfos) {
            const VhdlDTypeSpec& ps = portInfo.second.spec;
            key += "|P" + portInfo.first + "=" + std::to_string(ps.left) + ":"
                   + std::to_string(ps.right) + ":" + (ps.ascending ? "to" : "downto") + ":"
                   + std::to_string(ps.elemWidth);
            if (portInfo.second.record.dtypep) {
                key += "R";
                for (const RecordMember& member : portInfo.second.record.members) {
                    key += ":" + member.name + "/" + std::to_string(member.lsb) + "/"
                           + std::to_string(member.width);
                }
            }
        }
        const auto found = m_specializations.find(key);
        if (found != m_specializations.end()) return &found->second;
        if (m_specDepth >= 32) {
            error(entityp, "VHDL generic elaboration recursion is too deep");
            return nullptr;
        }
        const auto archIt = m_archByEntityNodes.find(entityKey);
        if (archIt == m_archByEntityNodes.end()) {
            error(entityp, "Entity has no architecture for generic specialization");
            return nullptr;
        }

        VL_RESTORER_COPY(m_constBindings);
        VL_RESTORER_COPY(m_typeBindings);
        for (const auto& binding : bindings) m_constBindings[binding.first] = binding.second;
        for (const auto& binding : typeBindings) m_typeBindings[binding.first] = binding.second;

        ++m_specDepth;
        ModuleInfo& info = m_specializations[key];
        const std::string modName
            = entityp->attr("identifier") + "__Vgen" + std::to_string(++m_genSeq);
        createModuleFor(entityp, info, modName, &portInfos);
        info.modp->inLibrary(true);
        populateModuleFor(entityp, archIt->second, info);
        --m_specDepth;
        return &info;
    }

    //! Default value expression of an entity port, if any
    const Node* portDefault(const Node* entityp, const std::string& foldedName) const {
        const Node* const portsp = entityp ? entityp->child("port_chain") : nullptr;
        if (!portsp) return nullptr;
        for (const Node* const portp : childEls(portsp)) {
            if (portp->attr("kind") != "interface_signal_declaration") continue;
            if (vhdlFold(portp->attr("identifier")) != foldedName) continue;
            return portp->child("default_value");
        }
        return nullptr;
    }

    //! GHDL's artificial open associations, resolved to the entity port they represent
    std::map<const Node*, std::string> artificialOpenPorts(const Node* stmtp,
                                                           const Node* entityp) const {
        std::map<const Node*, std::string> result;
        const Node* const pmap = stmtp->child("port_map_aspect_chain");
        if (!pmap || !entityp) return result;
        std::vector<std::string> ports;
        if (const Node* const portsp = entityp->child("port_chain")) {
            for (const Node* const portp : childEls(portsp)) {
                if (portp->attr("kind") == "interface_signal_declaration") {
                    ports.push_back(vhdlFold(portp->attr("identifier")));
                }
            }
        }
        std::set<std::string> used;
        for (const Node* const assocp : childEls(pmap)) {
            const Node* const formalp = assocp->child("formal");
            if (formalp && !formalp->attr("identifier").empty()) {
                used.insert(vhdlFold(formalp->attr("identifier")));
            }
        }
        size_t next = 0;
        for (const Node* const assocp : childEls(pmap)) {
            if (assocp->attr("kind") != "association_element_open") continue;
            const Node* const formalp = assocp->child("formal");
            if (formalp && !formalp->attr("identifier").empty()) continue;
            while (next < ports.size() && used.count(ports[next])) ++next;
            if (next >= ports.size()) continue;
            result[assocp] = ports[next];
            used.insert(ports[next]);
            ++next;
        }
        return result;
    }

    //! Ranges of an entity's unconstrained ports, taken from the instance's actuals
    bool gatherPortSpecs(const Node* stmtp, const Node* entityp,
                         std::map<std::string, PortInfo>& portInfos) {
        std::map<std::string, const Node*> actualByName;
        std::map<std::string, const Node*> individualByPort;
        std::vector<const Node*> positional;
        const std::map<const Node*, std::string> artificials = artificialOpenPorts(stmtp, entityp);
        if (const Node* const pmap = stmtp->child("port_map_aspect_chain")) {
            for (const Node* const assocp : childEls(pmap)) {
                const std::string kind = assocp->attr("kind");
                const Node* const formalp = assocp->child("formal");
                const std::string formalName
                    = formalp ? vhdlFold(formalp->attr("identifier")) : std::string{};
                if (artificials.find(assocp) != artificials.end()) continue;
                if (kind == "association_element_by_individual") {
                    // Formal is a record member or slice; find its base port name
                    const Node* basep = formalp;
                    for (int depth = 0; basep && depth < 16; ++depth) {
                        const std::string baseKind = basep->attr("kind");
                        if (baseKind == "simple_name") break;
                        const Node* const prefixp = basep->child("prefix");
                        if (!prefixp) {
                            basep = nullptr;
                            break;
                        }
                        basep = prefixp;
                    }
                    if (basep && basep->attr("kind") == "simple_name") {
                        individualByPort[vhdlFold(basep->attr("identifier"))] = assocp;
                    }
                    continue;
                }
                if (kind == "association_element_open") {
                    if (formalName.empty()) positional.push_back(nullptr);
                    continue;
                }
                const Node* const actualp = assocp->child("actual");
                if (!actualp) continue;
                if (!formalName.empty()) {
                    actualByName[formalName] = actualp;
                } else {
                    positional.push_back(actualp);
                }
            }
        }
        size_t position = 0;
        if (const Node* const portsp = entityp->child("port_chain")) {
            for (const Node* const portp : childEls(portsp)) {
                if (portp->attr("kind") != "interface_signal_declaration") continue;
                const std::string pname = vhdlFold(portp->attr("identifier"));
                const Node* actualp = nullptr;
                const auto namedIt = actualByName.find(pname);
                if (namedIt != actualByName.end()) {
                    actualp = namedIt->second;
                } else if (position < positional.size()) {
                    actualp = positional[position];
                }
                ++position;
                if (!probeTypeSpec(portp->child("subtype_indication")).isUnconstrained) {
                    continue;
                }
                if (!actualp) {
                    const auto individualIt = individualByPort.find(pname);
                    if (individualIt != individualByPort.end()) {
                        unsupported(individualIt->second, "individual record port association");
                        return false;
                    }
                    actualp = portp->child("default_value");
                }
                if (!actualp) {
                    error(stmtp, "Unconstrained port '" + portp->attr("identifier")
                                     + "' is not connected");
                    return false;
                }
                PortInfo portInfo;
                const Node* const typep = actualp->child("type");
                const Node* const defp
                    = typep && typep->has("ref") ? nodeById(typep->attr("ref")) : nullptr;
                if (!arraySpec(actualp, portInfo.spec)) {
                    // Records use their analyzed (constrained) type directly
                    const VhdlDTypeSpec probe = defp ? probeTypeSpec(defp) : VhdlDTypeSpec{};
                    if (!probe.dtp) {
                        unsupported(actualp, "unconstrained port actual type");
                        return false;
                    }
                    portInfo.spec = probe;
                }
                if (portInfo.spec.dtp && defp) recordLayout(defp, portInfo.record);
                portInfos[pname] = portInfo;
            }
        }
        return true;
    }

    void lowerInstance(const Node* stmtp) {
        FileLine* const flp = flpOf(stmtp);
        const Node* const unitp = stmtp->child("instantiated_unit");
        if (!unitp) {
            unsupported(stmtp, "instantiation without an instantiated unit");
            return;
        }

        const std::string unitName = instantiatedUnitName(unitp);
        if (unitName.empty()) {
            unsupported(unitp, "instantiated unit form '" + unitp->attr("kind") + "'");
            return;
        }
        const auto targetIt = m_modules.find(vhdlFold(unitName));
        ModuleInfo* targetp = targetIt == m_modules.end() ? nullptr : &targetIt->second;
        const auto entityIt = m_entityByName.find(vhdlFold(unitName));
        const Node* const entityp = entityIt == m_entityByName.end() ? nullptr : entityIt->second;
        // UG901: direct entity instantiation of a Verilog module is not supported
        const bool directEntity = unitp->attr("kind") == "entity_aspect_entity";
        if (directEntity && !targetp && !entityp) {
            error(unitp, "Unknown VHDL entity '" + unitName + "' in direct entity instantiation");
            return;
        }

        if (entityp) m_instantiatedEntities.insert(vhdlFold(unitName));

        std::map<std::string, int64_t> bindings;
        std::map<std::string, const Node*> typeBindings;
        std::map<std::string, PortInfo> portInfos;
        if (entityp && hasUnconstrainedPorts(entityp)
            && !gatherPortSpecs(stmtp, entityp, portInfos)) {
            return;
        }
        bool haveGenericMap = false;
        const Node* const gmapp = stmtp->child("generic_map_aspect_chain");
        if (gmapp) {
            haveGenericMap = true;
            if (!entityp) {
                unsupported(stmtp, "generic map on a non-VHDL unit");
                return;
            }
            const std::vector<GenericDecl> generics = genericsOf(entityp);
            std::set<std::string> genericNames;
            for (const GenericDecl& decl : generics) genericNames.insert(vhdlFold(decl.name));
            bool bad = false;
            for (const Node* const assocp : childEls(gmapp)) {
                const std::string kind = assocp->attr("kind");
                if (kind == "association_element_open") {
                    // GHDL emits an open association for an un-mapped generic; the default
                    // value is filled in below
                    continue;
                }
                if (kind == "association_element_type") {
                    // Generic type parameter: the actual is a type mark
                    const Node* const formalp = assocp->child("formal");
                    const Node* const actualp = assocp->child("actual");
                    if (!formalp || !actualp) {
                        unsupported(assocp, "generic type association without a type mark");
                        bad = true;
                        continue;
                    }
                    typeBindings[vhdlFold(formalp->attr("identifier"))] = actualp;
                    continue;
                }
                if (kind != "association_element_by_expression"
                    && kind != "association_element_by_name") {
                    unsupported(assocp, "generic association form '" + kind + "'");
                    bad = true;
                    continue;
                }
                const Node* const formalp = assocp->child("formal");
                const Node* const actualp = assocp->child("actual");
                int64_t value = 0;
                if (!formalp || !actualp || !evalConst(actualp, value)) {
                    unsupported(assocp, "non-static generic actual");
                    bad = true;
                    continue;
                }
                const std::string formalName = vhdlFold(formalp->attr("identifier"));
                if (!genericNames.count(formalName)) {
                    error(assocp, "Unknown generic '" + formalp->attr("identifier") + "' on '"
                                      + unitName + "'");
                    bad = true;
                    continue;
                }
                bindings[formalName] = value;
            }
            if (bad) return;
            for (const GenericDecl& decl : generics) {
                const std::string formalName = vhdlFold(decl.name);
                if (bindings.find(formalName) != bindings.end()) continue;
                int64_t value = 0;
                if (!decl.defaultp || !evalConst(decl.defaultp, value)) {
                    error(stmtp,
                          "Generic '" + decl.name + "' has no static default and is not mapped");
                    return;
                }
                bindings[formalName] = value;
            }
        }
        if (entityp && (haveGenericMap || !portInfos.empty())) {
            ModuleInfo* const spec = specializeModule(entityp, bindings, typeBindings, portInfos);
            if (!spec) return;
            targetp = spec;
        }

        if (targetp) {
            const auto compIt = m_components.find(vhdlFold(unitName));
            if (compIt != m_components.end()) {
                // Validate under the instance's bindings: the component's port ranges may
                // use its own generics, which specializeModule has since restored
                VL_RESTORER_COPY(m_constBindings);
                VL_RESTORER_COPY(m_typeBindings);
                for (const auto& binding : bindings) {
                    m_constBindings[binding.first] = binding.second;
                }
                for (const auto& binding : typeBindings) {
                    m_typeBindings[binding.first] = binding.second;
                }
                validateComponent(compIt->second, *targetp, unitName);
            }
        }
        // VHDL entity wins; otherwise leave the original spelling for the case-sensitive
        // SystemVerilog fallback (see VHDL_FRONTEND_PLAN.md section 3.5.1)
        const std::string modName = targetp ? targetp->modp->name() : unitName;

        AstPin* pinsp = nullptr;
        int positional = 0;
        const std::map<const Node*, std::string> artificials = artificialOpenPorts(stmtp, entityp);
        if (const Node* const mapChainp = stmtp->child("port_map_aspect_chain")) {
            for (const Node* const assocp : childEls(mapChainp)) {
                const std::string kind = assocp->attr("kind");
                std::string formalName;
                int pinNum = 0;
                const auto artIt = artificials.find(assocp);
                if (artIt != artificials.end()) {
                    formalName = artIt->second;
                } else if (kind == "association_element_by_name"
                           || kind == "association_element_by_expression"
                           || kind == "association_element_open") {
                    const Node* const formalp = assocp->child("formal");
                    formalName = formalp ? formalp->attr("identifier") : std::string{};
                    if (formalName.empty()) {
                        // A bare actual or open is a positional association
                        pinNum = ++positional;
                    }
                } else if (kind == "association_element_by_position") {
                    pinNum = ++positional;
                } else {
                    unsupported(assocp, "port association form '" + kind + "'");
                    continue;
                }

                AstVar* modVarp = nullptr;
                if (targetp) {
                    if (!formalName.empty()) {
                        const auto portIt = targetp->ports.find(vhdlFold(formalName));
                        if (portIt == targetp->ports.end()) {
                            error(assocp,
                                  "Unknown port '" + formalName + "' on '" + unitName + "'");
                            continue;
                        }
                        modVarp = portIt->second;
                    } else {
                        const int index = pinNum - 1;
                        if (index < 0 || index >= static_cast<int>(targetp->portList.size())) {
                            error(assocp, "Positional port " + std::to_string(pinNum)
                                              + " is out of range on '" + unitName + "'");
                            continue;
                        }
                        modVarp = targetp->portList[index];
                    }
                }

                const Node* const actualp = assocp->child("actual");
                AstNodeExpr* actualExprp = nullptr;
                if (actualp) {
                    // V3LinkLValue marks writable pin actuals as lvalues (WRITE); an inout
                    // connection is an lvalue here, not a read/write reference
                    if (!modVarp || !modVarp->isWritable()) {
                        actualExprp = lowerExpr(actualp, modVarp ? modVarp->dtypep() : nullptr);
                    } else {
                        const std::string actualKind = actualp->attr("kind");
                        if (actualKind != "simple_name" && actualKind != "indexed_name"
                            && actualKind != "slice_name" && actualKind != "selected_element") {
                            unsupported(actualp, "expression on a writable port connection");
                            continue;
                        }
                        actualExprp = lowerName(actualp, true);
                    }
                }
                FileLine* const pinFlp = flpOf(assocp);
                if (!actualExprp) {
                    if (modVarp && modVarp->isNonOutput()) {
                        const Node* const defp = portDefault(entityp, vhdlFold(modVarp->name()));
                        if (defp) {
                            actualExprp = lowerExpr(defp, modVarp->dtypep());
                        } else {
                            error(assocp,
                                  "open on an input or inout port without a default value");
                            continue;
                        }
                    }
                    // VHDL "open" on an output is an intentional disconnection
                    pinFlp->warnOff(V3ErrorCode::PINCONNECTEMPTY, true);
                }
                AstPin* const pinp = new AstPin{pinFlp, pinNum, formalName, actualExprp};
                pinp->modVarp(modVarp);
                pinsp = pinsp ? AstNode::addNext<AstPin, AstPin>(pinsp, pinp) : pinp;
            }
        }

        AstCell* const cellp = new AstCell{
            flp, flp, stmtp->attr("label") + m_genSuffix, modName, pinsp, nullptr, nullptr};
        m_modp->addStmtsp(cellp);
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
            m_archByEntityNodes[key] = archp;
        }

        std::set<std::string> entityKeys;
        std::vector<std::pair<const Node*, const Node*>> modules;  // Entity, architecture
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
            m_entityByName[vhdlFold(name)] = entityp;
            m_entityGenerics[vhdlFold(name)] = genericsOf(entityp);
            // A type-generic entity or one with unconstrained ports has no single port
            // type; only its specializations emit
            const bool unconstrainedPorts = hasUnconstrainedPorts(entityp);
            if (unconstrainedPorts && !v3Global.opt.topModule().empty()
                && vhdlFold(name) == vhdlFold(v3Global.opt.topModule())) {
                unsupported(entityp, "unconstrained port on a top-level entity");
            }
            if (!hasTypeGenerics(entityp) && !unconstrainedPorts) {
                modules.emplace_back(entityp, it->second);
            }
        }

        // Create all modules first so instances can bind to ports emitted later in the walk
        for (const auto& module : modules) createModule(module.first);
        for (const auto& module : modules) populateModule(module.first, module.second);

        // A generic entity's base module (default bindings) is instantiable but must not
        // compete as a top-level module when it is only reached through specializations
        std::set<std::string> instantiated;
        for (AstNodeModule* const modp : m_rootp->modulespList()) {
            for (AstNode* const stmtp : modp->stmtspList()) {
                if (const AstCell* const cellp = VN_CAST(stmtp, Cell)) {
                    instantiated.insert(cellp->modName());
                }
            }
        }
        for (auto& entry : m_modules) {
            const auto genIt = m_entityGenerics.find(entry.first);
            if (genIt != m_entityGenerics.end() && !genIt->second.empty() && entry.second.modp
                && (instantiated.count(entry.second.modp->name())
                    || m_instantiatedEntities.count(entry.first))) {
                entry.second.modp->inLibrary(true);
            }
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
    std::string ghdlFlags;
    if (const char* const envp = std::getenv("VERILATOR_GHDL_FLAGS")) {
        if (*envp) ghdlFlags = envp;
    }

    V3File::createMakeDir();
    const std::string makeDir = v3Global.opt.makeDir();
    const std::string xmlFile
        = V3Os::filenameJoin({makeDir, v3Global.opt.prefix() + "__vhdl.xml"});
    const std::string errFile
        = V3Os::filenameJoin({makeDir, v3Global.opt.prefix() + "__vhdl.err"});
    const auto quotePath = [](const std::string& path) { return "\"" + path + "\""; };

    std::string command = quotePath(ghdl);
    command += " --file-to-xml --std=08";
    if (!ghdlFlags.empty()) command += " " + ghdlFlags;
    command += " --workdir=" + quotePath(makeDir);
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
        v3Global.rootp()->fileline()->v3error("Cannot read GHDL XML output: '" + xmlFile
                                              + "'");  // LCOV_EXCL_LINE
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

// LCOV_EXCL_START
// The in-tree lexer/parser is reserved for a self-contained front end; it is not reachable
// from --language vhdl while the GHDL XML path is active, so no test can exercise it.

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

// LCOV_EXCL_STOP

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
        fileline->v3error(
            "Library map files are not supported for VHDL sources");  // LCOV_EXCL_LINE
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
