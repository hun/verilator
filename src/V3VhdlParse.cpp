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
    std::map<std::string, AstVar*> m_scope;  // Current module: folded name -> Var
    std::map<std::string, int64_t> m_constBindings;  // Loop parameters during unrolling
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
    std::map<const Node*, AstNodeDType*> m_enumDtypes;  // Enum type definition -> dtype
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
    std::map<const Node*, RecordInfo> m_records;  // Record type definition -> layout
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

    //! Build (once) the Verilator enum dtype for an enumeration type definition
    AstNodeDType* enumDType(const Node* typeDefp) {
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

    //! Build (once) the packed-vector layout of a record type definition
    const RecordInfo& recordInfo(const Node* typeDefp) {
        const auto found = m_records.find(typeDefp);
        if (found != m_records.end()) return found->second;
        RecordInfo& info = m_records[typeDefp];
        FileLine* const flp = flpOf(typeDefp);
        int width = 0;
        bool fourstate = false;
        for (const Node* const elemp : childEls(typeDefp->child("elements_declaration_list"))) {
            RecordMember member;
            member.name = elemp->attr("identifier");
            member.dtypep = dtypeOf(parseType(elemp->child("subtype_indication")), flpOf(elemp));
            member.width = member.dtypep->width();
            width += member.width;
            fourstate |= member.dtypep->isFourstate();
            info.members.push_back(member);
        }
        // The first declared element occupies the most significant bits
        int lsb = width;
        for (RecordMember& member : info.members) {
            lsb -= member.width;
            member.lsb = lsb;
        }
        info.dtypep = m_rootp->typeTablep()->findLogicBitDType(
            flp, fourstate ? VBasicDTypeKwd::LOGIC : VBasicDTypeKwd::BIT, width, width,
            VSigning::UNSIGNED);
        return info;
    }

    //! Declarations resolved at their use sites, or not modeled by Verilator
    static bool ignorableDeclaration(const std::string& kind) {
        return kind == "constant_declaration" || kind == "type_declaration"
               || kind == "anonymous_type_declaration" || kind == "subtype_declaration"
               || kind == "function_declaration" || kind == "procedure_declaration"
               || kind == "attribute_declaration" || kind == "attribute_specification"
               || kind == "alias_declaration" || kind == "file_declaration"
               || kind == "use_clause";
    }

    VhdlDTypeSpec parseType(const Node* subtypep, int depth = 0) {
        VhdlDTypeSpec spec;
        if (!subtypep || depth > 16) return spec;
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
            if (id == "std_logic" || id == "boolean") {
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
                            if (subp) return parseType(subp, depth + 1);
                            if (const Node* const defp = declp->child("type_definition")) {
                                if (defp->attr("kind") == "enumeration_type_definition") {
                                    spec.dtp = enumDType(defp);
                                    return spec;
                                }
                                if (defp->attr("kind") == "record_type_definition") {
                                    spec.dtp = recordInfo(defp).dtypep;
                                    return spec;
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
        if (kind != "array_subtype_definition") {
            error(subtypep, "VHDL type form '" + kind + "' is not supported yet");
            return spec;
        }

        const Node* const markp = subtypep->child("subtype_type_mark");
        std::string mark = markp ? vhdlFold(markp->attr("identifier")) : std::string{};
        if (mark.empty()) {
            // Anonymous array type (type X is array (...) of T): derive from the element type
            const Node* const elemRefp = subtypep->child("element_subtype");
            const Node* elemp = nullptr;
            if (elemRefp) {
                elemp = elemRefp->has("ref") ? nodeById(elemRefp->attr("ref")) : elemRefp;
            }
            if (elemp) {
                if (const Node* const elemMarkp = elemp->child("subtype_type_mark")) {
                    mark = vhdlFold(elemMarkp->attr("identifier"));
                } else if (elemp->attr("kind") == "simple_name") {
                    mark = vhdlFold(elemp->attr("identifier"));
                }
            }
        }
        if (mark == "signed") {
            spec.kwd = VBasicDTypeKwd::LOGIC;
            spec.signing = VSigning::SIGNED;
        } else if (mark == "unsigned" || mark == "std_logic_vector" || mark == "std_logic"
                   || mark == "std_ulogic") {
            spec.kwd = VBasicDTypeKwd::LOGIC;
            spec.signing = VSigning::UNSIGNED;
        } else if (mark == "bit_vector" || mark == "bit") {
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
        return spec;
    }

    AstNodeDType* dtypeOf(const VhdlDTypeSpec& spec, FileLine* flp) const {
        if (spec.dtp) return spec.dtp;
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

    //! Lower a simple, indexed, sliced or selected name; write selects lvalue access
    AstNodeExpr* lowerName(const Node* nodep, bool write) {
        const std::string kind = nodep->attr("kind");
        if (kind == "selected_element") {
            // Record member: a bit slice of the packed record vector
            AstNodeExpr* const basep = lowerName(nodep->child("prefix"), write);
            const Node* const defp
                = typeDefOfKind(nodep->child("prefix"), "record_type_definition");
            if (!defp) {
                error(nodep, "VHDL selection is not a record member");
                return basep;
            }
            const std::string name = vhdlFold(nodep->attr("identifier"));
            for (const RecordMember& member : recordInfo(defp).members) {
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
        if (kind == "indexed_name") {
            const std::vector<const Node*> indices = childEls(nodep->child("index_list"));
            if (indices.size() != 1) {
                unsupported(nodep, "multi-dimensional VHDL index");
                return basep;
            }
            const int baseIndex = basicp->ascending() ? basicp->left() : basicp->right();
            if (indices.front()->attr("kind") == "integer_literal") {
                const int index = static_cast<int>(
                    std::strtol(indices.front()->attr("value").c_str(), nullptr, 10));
                const int offset = index - baseIndex;
                if (offset < 0 || offset >= basicp->width()) {
                    error(nodep, "VHDL index is out of range");
                    return basep;
                }
                return new AstSel{flpOf(nodep), basep, offset, 1};
            }
            AstNodeExpr* const indexExprp = lowerExpr(indices.front(), intDType(flpOf(nodep)));
            if (!indexExprp) return basep;
            AstNodeExpr* const offsetp
                = new AstSub{flpOf(nodep), indexExprp,
                             makeConst(flpOf(nodep), intDType(flpOf(nodep)), baseIndex)};
            offsetp->dtypep(intDType(flpOf(nodep)));
            return new AstSel{flpOf(nodep), basep, offsetp, 1};
        }

        // slice_name
        const Node* const suffixp = nodep->child("suffix");
        if (!suffixp || suffixp->attr("kind") != "range_expression") {
            unsupported(nodep, "VHDL slice form");
            return basep;
        }
        int left = 0;
        int right = 0;
        if (!literalValue(suffixp->child("left_limit_expr"), left)
            || !literalValue(suffixp->child("right_limit_expr"), right)) {
            unsupported(nodep, "non-literal VHDL slice bounds");
            return basep;
        }
        const bool ascending = suffixp->attr("direction") == "to";
        if (ascending != basicp->ascending()) {
            error(nodep, "VHDL slice direction does not match the array");
            return basep;
        }
        const int lowIndex = ascending ? left : right;
        const int offset = ascending ? lowIndex - basicp->left() : lowIndex - basicp->right();
        const int width = std::abs(left - right) + 1;
        if (offset < 0 || offset + width > basicp->width()) {
            error(nodep, "VHDL slice is out of range");
            return basep;
        }
        AstNodeExpr* const selp = new AstSel{flpOf(nodep), basep, offset, width};
        // AstSel defaults to unsigned; a slice of a signed array stays signed
        selp->dtypep(m_rootp->typeTablep()->findLogicBitDType(flpOf(nodep), basicp->keyword(),
                                                              width, width, basicp->numeric()));
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
            int width = 0;
            if (!valuep || !widthp || !literalValue(widthp, width) || width <= 0) {
                unsupported(nodep, "non-static width in " + fn);
                return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
            }
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

        unsupported(nodep, "function call '" + fn + "'");
        return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
    }

    //! Lower a record aggregate: concatenate members, most significant first
    AstNodeExpr* lowerRecordAggregate(const Node* nodep, const Node* typeDefp) {
        FileLine* const flp = flpOf(nodep);
        const RecordInfo& info = recordInfo(typeDefp);
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
                unsupported(choicep, "VHDL aggregate choice '" + choiceKind + "'");
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

    AstNodeExpr* lowerExpr(const Node* nodep, AstNodeDType* contextp) {
        if (!nodep) return nullptr;
        FileLine* const flp = flpOf(nodep);
        const std::string kind = nodep->attr("kind");

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
            AstNodeDType* const dtp = contextp ? contextp : bitDType(flp);
            if (content.empty()) {
                unsupported(nodep, "empty VHDL string literal");
                return makeConst(flp, dtp, 0);
            }
            uint32_t value = 0;
            for (const char ch : content) {
                if (ch != '0' && ch != '1') {
                    unsupported(nodep, "non-binary VHDL string literal");
                    return makeConst(flp, dtp, 0);
                }
                value = (value << 1) | (ch == '1' ? 1U : 0U);
            }
            if (static_cast<int>(content.size()) != dtp->width()) {
                error(nodep, "VHDL string literal length does not match its context");
                return makeConst(flp, dtp, 0);
            }
            return makeConst(flp, dtp, value);
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
            return resizeExpr(innerp, contextp);
        }
        if (kind == "aggregate") {
            const Node* const defp = typeDefOfKind(nodep, "record_type_definition");
            if (defp) return lowerRecordAggregate(nodep, defp);
            unsupported(nodep, "VHDL aggregate of a non-record type");
            return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
        }
        if (kind == "simple_aggregate") {
            // GHDL folds locally static expressions into an aggregate that retains the
            // originating call; lower that call instead
            if (const Node* const originp = nodep->child("literal_origin")) {
                return lowerExpr(originp, contextp);
            }
            unsupported(nodep, "static VHDL aggregate");
            return makeConst(flp, contextp ? contextp : bitDType(flp), 0);
        }
        if (kind == "function_call") { return lowerCall(nodep, contextp); }
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

    AstNode* lowerSelectedAssignment(const Node* stmtp, AssignMode mode) {
        FileLine* const flp = flpOf(stmtp);
        const Node* const targetp = targetNode(stmtp);
        AstNodeExpr* const selectorp = lowerExpr(stmtp->child("expression"), nullptr);
        if (!selectorp) return nullptr;

        AstCaseItem* itemsp = nullptr;
        AstCaseItem* currentp = nullptr;
        for (const Node* const choicep : childEls(stmtp->child("selected_waveform_chain"))) {
            AstConst* condp = nullptr;
            const std::string kind = choicep->attr("kind");
            if (kind == "choice_by_expression") {
                const Node* const choiceExprp = choicep->child("choice_expression");
                if (!choiceExprp) {
                    unsupported(choicep, "selected choice without an expression");
                    continue;
                }
                AstNodeExpr* const choiceValp = lowerExpr(choiceExprp, selectorp->dtypep());
                if (!choiceValp) continue;
                condp = VN_CAST(choiceValp, Const);
                if (!condp) {
                    VL_DO_DANGLING(choiceValp->deleteTree(), choiceValp);
                    unsupported(choicep, "non-constant VHDL selected choice");
                    continue;
                }
            } else if (kind != "choice_by_others") {
                unsupported(choicep, "selected choice form '" + kind + "'");
                continue;
            }

            const bool sameAlternative = choicep->attr("same_alternative_flag") == "true";
            if (!sameAlternative || !currentp) {
                AstNode* bodyp = nullptr;
                for (const Node* const wavep : childEls(choicep->child("associated_chain"))) {
                    if (wavep->attr("kind") != "waveform_element") {
                        unsupported(wavep,
                                    "selected association form '" + wavep->attr("kind") + "'");
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
                currentp = new AstCaseItem{flp, condp, bodyp};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
            } else if (condp) {
                currentp->addCondsp(condp);
            }
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
            AstConst* condp = nullptr;
            const std::string kind = choicep->attr("kind");
            if (kind == "choice_by_expression") {
                const Node* const choiceExprp = choicep->child("choice_expression");
                if (!choiceExprp) {
                    unsupported(choicep, "case choice without an expression");
                    continue;
                }
                AstNodeExpr* const choiceValp = lowerExpr(choiceExprp, selectorp->dtypep());
                if (!choiceValp) continue;
                // AstCaseItem conditions are constant values; V3Case builds the comparisons
                condp = VN_CAST(choiceValp, Const);
                if (!condp) {
                    VL_DO_DANGLING(choiceValp->deleteTree(), choiceValp);
                    unsupported(choicep, "non-constant VHDL case choice");
                    continue;
                }
            } else if (kind != "choice_by_others") {
                unsupported(choicep, "case choice form '" + kind + "'");
                continue;
            }

            const bool sameAlternative = choicep->attr("same_alternative_flag") == "true";
            if (!sameAlternative || !currentp) {
                AstNode* const bodyp = lowerStmtChain(choicep->child("associated_chain"), mode);
                currentp = new AstCaseItem{flp, condp, bodyp};
                itemsp = itemsp ? AstNode::addNext<AstCaseItem, AstCaseItem>(itemsp, currentp)
                                : currentp;
            } else if (condp) {
                currentp->addCondsp(condp);
            }
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
        if (!varp) return false;
        const AstBasicDType* const basicp = VN_CAST(varp->dtypep()->skipRefp(), BasicDType);
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
            return false;
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
        const Node* boundp = rangep->child(leftBound ? "left_limit_expr" : "right_limit_expr");
        if (boundp) return boundp;
        boundp = rangep->child(leftBound ? "left_limit" : "right_limit");
        if (!boundp) return nullptr;
        if (boundp->has("ref")) return nodeById(boundp->attr("ref"));
        return boundp->has("kind") ? boundp : nullptr;
    }

    //! Evaluate a discrete range to integer bounds and direction
    bool discreteRange(const Node* rangep, int& left, int& right, bool& ascending) const {
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
            unsupported(stmtp, "non-static VHDL loop range");
            return nullptr;
        }

        const std::string key = vhdlFold(name);
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
        return headp;
    }

    AstNode* lowerStmt(const Node* stmtp, AssignMode mode) {
        const std::string kind = stmtp->attr("kind");
        if (kind == "simple_signal_assignment_statement") { return lowerAssignment(stmtp, mode); }
        if (kind == "variable_assignment_statement") return lowerVariableAssignment(stmtp);
        if (kind == "case_statement") return lowerCase(stmtp, mode);
        if (kind == "for_loop_statement") return lowerForLoop(stmtp, mode);
        if (kind == "selected_waveform_assignment_statement") {
            return lowerSelectedAssignment(stmtp, mode);
        }
        if (kind == "null_statement") return nullptr;
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
        AstNode* const bodyp
            = lowerStmtChain(processp->child("sequential_statement_chain"), AssignMode::CONT);
        if (!bodyp) return;
        m_modp->addStmtsp(new AstAlways{flpOf(processp), VAlwaysKwd::CONT_ASSIGN, nullptr, bodyp});
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

    //! Recognize the "clk'event and clk = '1'" clock idiom
    bool eventEdge(const Node* condp, EdgeInfo& info) {
        if (!condp || condp->attr("kind") != "and_operator") return false;
        const Node* const lhsp = condp->child("left");
        const Node* const rhsp = condp->child("right");
        const Node* eventp = nullptr;
        const Node* cmpp = nullptr;
        if (lhsp && lhsp->attr("kind") == "event_attribute") {
            eventp = lhsp;
            cmpp = rhsp;
        } else if (rhsp && rhsp->attr("kind") == "event_attribute") {
            eventp = rhsp;
            cmpp = lhsp;
        } else {
            return false;
        }
        if (!cmpp || cmpp->attr("kind") != "equality_operator") return false;
        const Node* const prefixp = eventp->child("prefix");
        if (!prefixp || prefixp->attr("kind") != "simple_name") return false;
        const Node* const cmpLhsp = cmpp->child("left");
        const Node* const cmpRhsp = cmpp->child("right");
        const Node* litp = nullptr;
        const Node* namep = nullptr;
        if (cmpLhsp && cmpLhsp->attr("kind") == "character_literal") {
            litp = cmpLhsp;
            namep = cmpRhsp;
        } else if (cmpRhsp && cmpRhsp->attr("kind") == "character_literal") {
            litp = cmpRhsp;
            namep = cmpLhsp;
        }
        if (!litp || !namep || namep->attr("kind") != "simple_name") return false;
        if (vhdlFold(namep->attr("identifier")) != vhdlFold(prefixp->attr("identifier"))) {
            return false;
        }
        const std::string lit = litp->attr("identifier");
        if (lit != "'1'" && lit != "'0'") return false;
        info.edgeType = lit == "'1'" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
        info.sigp = lowerExpr(prefixp, nullptr);
        return info.sigp != nullptr;
    }

    //! Extract the reset signal/edge from a supported reset condition; also lowers the condition
    bool resetEdge(const Node* condp, AstNodeExpr*& sigp, VEdgeType& edgeType,
                   AstNodeExpr*& condExprp) {
        const std::string kind = condp->attr("kind");
        if (kind == "equality_operator") {
            const Node* const lhsp = condp->child("left");
            const Node* const rhsp = condp->child("right");
            const Node* litp = nullptr;
            const Node* namep = nullptr;
            if (lhsp && lhsp->attr("kind") == "character_literal") {
                litp = lhsp;
                namep = rhsp;
            } else if (rhsp && rhsp->attr("kind") == "character_literal") {
                litp = rhsp;
                namep = lhsp;
            }
            if (litp && namep && namep->attr("kind") == "simple_name") {
                const std::string lit = litp->attr("identifier");
                if (lit == "'1'" || lit == "'0'") {
                    edgeType = lit == "'1'" ? VEdgeType::ET_POSEDGE : VEdgeType::ET_NEGEDGE;
                    sigp = lowerExpr(namep, nullptr);
                    condExprp = lowerExpr(condp, bitDType(flpOf(condp)));
                    return sigp && condExprp;
                }
            }
        } else if (kind == "simple_name") {
            edgeType = VEdgeType::ET_POSEDGE;
            sigp = lowerExpr(condp, nullptr);
            condExprp = lowerExpr(condp, bitDType(flpOf(condp)));
            return sigp && condExprp;
        } else if (kind == "not_operator") {
            const Node* const operandp = condp->child("operand");
            if (operandp && operandp->attr("kind") == "simple_name") {
                edgeType = VEdgeType::ET_NEGEDGE;
                sigp = lowerExpr(operandp, nullptr);
                condExprp = lowerExpr(condp, bitDType(flpOf(condp)));
                return sigp && condExprp;
            }
        }
        unsupported(condp, "asynchronous reset condition");
        return false;
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
                AstNodeExpr* const rhsp = lowerDefault(defp, varp->dtypep());
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

        // Clocked idiom: a single "if rising_edge(clk)"/"if falling_edge(clk)" wrapper
        if (const Node* const ifp = singleTopIf(bodyp)) {
            EdgeInfo clock;
            if (edgeCall(ifp->child("condition"), clock)
                || eventEdge(ifp->child("condition"), clock)) {
                if (ifp->child("else_clause")) {
                    unsupported(ifp, "else branch in a clocked process");
                    return;
                }
                AstSenItem* const itemp = new AstSenItem{flp, clock.edgeType, clock.sigp};
                AstNode* const stmtsp = lowerStmtChain(ifp->child("sequential_statement_chain"),
                                                       AssignMode::DELAYED);
                m_modp->addStmtsp(
                    new AstAlways{flp, VAlwaysKwd::ALWAYS, new AstSenTree{flp, itemp}, stmtsp});
                return;
            }

            // Asynchronous reset: "if reset then ... elsif edge(clk) then ..."
            const Node* const elsep = ifp->child("else_clause");
            EdgeInfo clock2;
            if (elsep && elsep->attr("kind") == "elsif" && !elsep->child("else_clause")
                && (edgeCall(elsep->child("condition"), clock2)
                    || eventEdge(elsep->child("condition"), clock2))) {
                AstNodeExpr* rstSigp = nullptr;
                VEdgeType rstEdge = VEdgeType::ET_CHANGED;
                AstNodeExpr* rstCondp = nullptr;
                if (!resetEdge(ifp->child("condition"), rstSigp, rstEdge, rstCondp)) return;
                // A reset signal in the sensitivity list makes the reset asynchronous
                AstVar* const rstVarp = astVarOf(rstSigp);
                const bool asyncReset
                    = rstVarp && sensitivityHasVar(processp->child("sensitivity_list"), rstVarp);
                AstSenItem* const clkItemp = new AstSenItem{flp, clock2.edgeType, clock2.sigp};
                AstSenTree* senTreep = nullptr;
                if (asyncReset) {
                    AstSenItem* const rstItemp = new AstSenItem{flp, rstEdge, rstSigp};
                    senTreep = new AstSenTree{
                        flp, AstNode::addNext<AstSenItem, AstSenItem>(clkItemp, rstItemp)};
                } else {
                    senTreep = new AstSenTree{flp, clkItemp};
                }
                AstNode* const resetStmtsp = lowerStmtChain(
                    ifp->child("sequential_statement_chain"), AssignMode::DELAYED);
                AstNode* const normalStmtsp = lowerStmtChain(
                    elsep->child("sequential_statement_chain"), AssignMode::DELAYED);
                m_modp->addStmtsp(
                    new AstAlways{flp, VAlwaysKwd::ALWAYS, senTreep,
                                  new AstIf{flp, rstCondp, resetStmtsp, normalStmtsp}});
                // The process also executes once at time zero; a level-true reset applies then
                AstNodeExpr* const initCondp = lowerExpr(ifp->child("condition"), bitDType(flp));
                AstNode* const initResetp = lowerStmtChain(
                    ifp->child("sequential_statement_chain"), AssignMode::BLOCKING);
                if (initResetp) {
                    m_modp->addStmtsp(
                        new AstInitialStatic{flp, new AstIf{flp, initCondp, initResetp}});
                }
                return;
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
            unsupported(stmtp, "non-static VHDL generate range");
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
                AstNodeExpr* const rhsp = lowerDefault(defp, varp->dtypep());
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

    void createModuleFor(const Node* entityp, ModuleInfo& info, const std::string& modName) {
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
                AstVar* const varp
                    = new AstVar{pflp, VVarType::PORT, pname, dtypeOf(parseType(subtypep), pflp)};
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
        VL_RESTORER_COPY(m_components);
        m_modp = info.modp;
        m_scope = info.ports;
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
                const int width = dtypeOf(parseType(subtypep), flpOf(portp))->width();
                if (width != varp->width()) {
                    error(portp, "Component '" + unitName + "' port '" + name + "' width "
                                     + std::to_string(width) + " does not match the entity ("
                                     + std::to_string(varp->width()) + ")");
                }
            }
        }
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
                                 const std::map<std::string, int64_t>& bindings) {
        const std::string entityKey = vhdlFold(entityp->attr("identifier"));
        std::string key = entityKey;
        for (const auto& binding : bindings) {
            key += "|" + binding.first + "=" + std::to_string(binding.second);
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
        for (const auto& binding : bindings) m_constBindings[binding.first] = binding.second;

        ++m_specDepth;
        ModuleInfo& info = m_specializations[key];
        const std::string modName
            = entityp->attr("identifier") + "__Vgen" + std::to_string(++m_genSeq);
        createModuleFor(entityp, info, modName);
        info.modp->inLibrary(true);
        populateModuleFor(entityp, archIt->second, info);
        --m_specDepth;
        return &info;
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
        if (directEntity && !targetp) {
            error(unitp, "Unknown VHDL entity '" + unitName + "' in direct entity instantiation");
            return;
        }

        if (entityp) m_instantiatedEntities.insert(vhdlFold(unitName));

        std::map<std::string, int64_t> bindings;
        if (const Node* const gmapp = stmtp->child("generic_map_aspect_chain")) {
            if (!targetp || !entityp) {
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
            ModuleInfo* const spec = specializeModule(entityp, bindings);
            if (!spec) return;
            targetp = spec;
        }

        if (targetp) {
            const auto compIt = m_components.find(vhdlFold(unitName));
            if (compIt != m_components.end()) {
                // Validate under the instance's bindings: the component's port ranges may
                // use its own generics, which specializeModule has since restored
                VL_RESTORER_COPY(m_constBindings);
                for (const auto& binding : bindings) {
                    m_constBindings[binding.first] = binding.second;
                }
                validateComponent(compIt->second, *targetp, unitName);
            }
        }
        // VHDL entity wins; otherwise leave the original spelling for the case-sensitive
        // SystemVerilog fallback (see VHDL_FRONTEND_PLAN.md section 3.5.1)
        const std::string modName = targetp ? targetp->modp->name() : unitName;

        AstPin* pinsp = nullptr;
        int positional = 0;
        if (const Node* const mapChainp = stmtp->child("port_map_aspect_chain")) {
            for (const Node* const assocp : childEls(mapChainp)) {
                const std::string kind = assocp->attr("kind");
                std::string formalName;
                int pinNum = 0;
                if (kind == "association_element_by_name" || kind == "association_element_open") {
                    const Node* const formalp = assocp->child("formal");
                    formalName = formalp ? formalp->attr("identifier") : std::string{};
                    if (formalName.empty()) {
                        unsupported(assocp, "port association without a formal");
                        continue;
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
                    actualExprp = lowerExpr(actualp, modVarp ? modVarp->dtypep() : nullptr);
                    if (actualExprp && modVarp) {
                        VAccess access = VAccess::READ;
                        if (modVarp->isInout()) {
                            access = VAccess::READWRITE;
                        } else if (modVarp->isWritable()) {
                            access = VAccess::WRITE;
                        }
                        if (AstNodeVarRef* const refp = VN_CAST(actualExprp, NodeVarRef)) {
                            refp->access(access);
                        } else if (access != VAccess::READ) {
                            unsupported(actualp, "expression on a writable port connection");
                            continue;
                        }
                    }
                }
                FileLine* const pinFlp = flpOf(assocp);
                if (!actualExprp) {
                    if (modVarp && modVarp->isNonOutput()) {
                        error(assocp, "open on an input or inout port with a default value is not "
                                      "supported yet");
                        continue;
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
            modules.emplace_back(entityp, it->second);
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
