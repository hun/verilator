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

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlXml.h"

#include "V3Error.h"
#include "V3FileLine.h"

#include <cctype>

namespace {

const std::string kEmptyString;

//! Decode the XML character references GHDL emits in attribute values
std::string decodeEntities(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] != '&') {
            result += text[pos++];
            continue;
        }
        const size_t semi = text.find(';', pos + 1);
        if (semi == std::string::npos) {
            result += text[pos++];
            continue;
        }
        const std::string entity = text.substr(pos + 1, semi - pos - 1);
        if (entity == "lt") {
            result += '<';
        } else if (entity == "gt") {
            result += '>';
        } else if (entity == "amp") {
            result += '&';
        } else if (entity == "quot") {
            result += '"';
        } else if (entity == "apos") {
            result += '\'';
        } else if (!entity.empty() && entity[0] == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string digits = entity.substr(hex ? 2 : 1);
            const unsigned long code = std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
            if (code && code < 0x80) result += static_cast<char>(code);
        } else {
            result += text.substr(pos, semi - pos + 1);
        }
        pos = semi + 1;
    }
    return result;
}

//######################################################################
// Recursive descent parser for the subset of XML GHDL emits

class XmlParser final {
    FileLine* const m_flp;
    const std::string& m_text;
    size_t m_pos = 0;
    bool m_failed = false;

    bool eof() const { return m_pos >= m_text.size(); }
    char peek() const { return eof() ? '\0' : m_text[m_pos]; }

    bool startsWith(const char* text) const {
        const size_t len = std::strlen(text);
        return m_text.compare(m_pos, len, text) == 0;
    }

    void skipSpace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
    }

    void fail(const std::string& message) {
        if (m_failed) return;
        m_failed = true;
        m_flp->v3error("Malformed GHDL XML at offset " + std::to_string(m_pos) + ": " + message);
    }

    void skipUntil(const char* terminator) {
        const size_t end = m_text.find(terminator, m_pos);
        if (end == std::string::npos) {
            fail(std::string{"missing '"} + terminator + "'");
            m_pos = m_text.size();
            return;
        }
        m_pos = end + std::strlen(terminator);
    }

    std::string parseName() {
        const size_t start = m_pos;
        while (!eof() && !std::isspace(static_cast<unsigned char>(peek())) && peek() != '>'
               && peek() != '/' && peek() != '=') {
            ++m_pos;
        }
        if (start == m_pos) fail("expected a name");
        return m_text.substr(start, m_pos - start);
    }

    std::string parseAttributeValue() {
        if (eof() || (peek() != '"' && peek() != '\'')) {
            fail("expected a quoted attribute value");
            return {};
        }
        const char quote = m_text[m_pos++];
        const size_t start = m_pos;
        const size_t end = m_text.find(quote, start);
        if (end == std::string::npos) {
            fail("unterminated attribute value");
            m_pos = m_text.size();
            return {};
        }
        m_pos = end + 1;
        return decodeEntities(m_text.substr(start, end - start));
    }

    //! Parse a start element, its children and its end tag. Returns nullptr on failure.
    std::unique_ptr<V3VhdlXmlNode> parseElement() {
        if (peek() != '<') {
            fail("expected '<'");
            return nullptr;
        }
        ++m_pos;
        std::unique_ptr<V3VhdlXmlNode> nodep{new V3VhdlXmlNode{parseName()}};
        if (m_failed) return nullptr;

        while (true) {
            skipSpace();
            if (eof()) {
                fail("unexpected end of document in start tag");
                return nullptr;
            }
            if (peek() == '/') {
                ++m_pos;
                if (peek() != '>') {
                    fail("expected '>' after '/'");
                    return nullptr;
                }
                ++m_pos;
                return nodep;  // Self-closing
            }
            if (peek() == '>') {
                ++m_pos;
                break;
            }
            const std::string key = parseName();
            if (m_failed) return nullptr;
            skipSpace();
            if (peek() != '=') {
                fail("expected '=' after attribute name '" + key + "'");
                return nullptr;
            }
            ++m_pos;
            skipSpace();
            nodep->attr(key, parseAttributeValue());
            if (m_failed) return nullptr;
        }

        // Children
        while (true) {
            skipSpace();
            if (eof()) {
                fail("unexpected end of document in element '" + nodep->name() + "'");
                return nullptr;
            }
            if (startsWith("</")) {
                m_pos += 2;
                const std::string closeName = parseName();
                if (m_failed) return nullptr;
                skipSpace();
                if (peek() != '>') {
                    fail("expected '>' in end tag");
                    return nullptr;
                }
                ++m_pos;
                if (closeName != nodep->name()) {
                    fail("end tag '" + closeName + "' does not match '" + nodep->name() + "'");
                    return nullptr;
                }
                return nodep;
            }
            if (startsWith("<!--")) {
                skipUntil("-->");
                continue;
            }
            if (startsWith("<?")) {
                skipUntil("?>");
                continue;
            }
            if (startsWith("<!")) {
                skipUntil(">");
                continue;
            }
            if (peek() == '<') {
                std::unique_ptr<V3VhdlXmlNode> childp = parseElement();
                if (!childp) return nullptr;
                nodep->addChild(std::move(childp));
                continue;
            }
            // Stray text carries no information
            while (!eof() && peek() != '<') ++m_pos;
        }
    }

public:
    XmlParser(FileLine* flp, const std::string& text)
        : m_flp{flp}
        , m_text{text} {}
    ~XmlParser() = default;

    std::unique_ptr<V3VhdlXmlNode> parse() {
        while (true) {
            skipSpace();
            if (eof()) {
                fail("no root element");
                return nullptr;
            }
            if (startsWith("<?")) {
                skipUntil("?>");
                continue;
            }
            if (startsWith("<!--")) {
                skipUntil("-->");
                continue;
            }
            break;
        }
        std::unique_ptr<V3VhdlXmlNode> rootp = parseElement();
        if (!rootp) return nullptr;
        skipSpace();
        if (!eof()) {
            fail("trailing content after the root element");
            return nullptr;
        }
        return rootp;
    }
};

const std::string& attributeValue(const V3VhdlXmlNode::Attributes& attrs, const std::string& key) {
    const auto it = attrs.find(key);
    return it == attrs.end() ? kEmptyString : it->second;
}

}  // namespace

const std::string& V3VhdlXmlNode::attr(const std::string& key) const {
    return attributeValue(m_attrs, key);
}

const V3VhdlXmlNode* V3VhdlXmlNode::child(const std::string& name) const {
    for (const std::unique_ptr<V3VhdlXmlNode>& childp : m_children) {
        if (childp->name() == name) return childp.get();
    }
    return nullptr;
}

std::unique_ptr<V3VhdlXmlNode> V3VhdlXml::parse(FileLine* flp, const std::string& text) {
    XmlParser parser{flp, text};
    return parser.parse();
}
