// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Minimal XML tree for GHDL --file-to-xml output
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

#ifndef VERILATOR_V3VHDLGHDLXML_H_
#define VERILATOR_V3VHDLGHDLXML_H_

#include "config_build.h"
#include "verilatedos.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class FileLine;

//! One element of a GHDL `--file-to-xml` document.
//!
//! GHDL writes every intermediate-representation node as an `<el>` (or field-named) element with
//! `id`, `kind`, `file`, `line` and `col` attributes, and nests the node's fields as child
//! elements. Cross references are empty elements with a `ref` attribute naming another node's
//! `id`. Text content carries no information and is dropped while parsing.
class V3VhdlXmlNode final {
public:
    using Attributes = std::map<std::string, std::string>;

private:
    std::string m_name;  // Element name
    Attributes m_attrs;  // Attribute name/value pairs
    std::vector<std::unique_ptr<V3VhdlXmlNode>> m_children;  // Child elements

public:
    explicit V3VhdlXmlNode(std::string name)
        : m_name{std::move(name)} {}
    ~V3VhdlXmlNode() = default;

    const std::string& name() const { return m_name; }
    const Attributes& attrs() const { return m_attrs; }
    //! Set one attribute
    void attr(const std::string& key, std::string value) {
        m_attrs.emplace(key, std::move(value));
    }
    bool has(const std::string& key) const { return m_attrs.find(key) != m_attrs.end(); }
    //! Attribute value, or an empty string when the attribute is absent
    const std::string& attr(const std::string& key) const;

    void addChild(std::unique_ptr<V3VhdlXmlNode> childp) {
        m_children.push_back(std::move(childp));
    }
    const std::vector<std::unique_ptr<V3VhdlXmlNode>>& children() const { return m_children; }
    //! First direct child with the given element name, or nullptr
    const V3VhdlXmlNode* child(const std::string& name) const;
};

class V3VhdlXml final {
public:
    //! Parse a GHDL XML document. Malformed input is reported on flp and returns nullptr.
    static std::unique_ptr<V3VhdlXmlNode> parse(FileLine* flp, const std::string& text);
};

#endif  // Guard
