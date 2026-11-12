// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Self-contained VHDL frontend: lexer
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
// VHDL lexer: source text to tokens. Handles the two comment forms, the
// character-literal vs. attribute-mark ambiguity of the apostrophe, based
// literals and bit-string literals.
//
//*************************************************************************

#include "V3PchAstNoMT.h"  // VL_MT_DISABLED_CODE_UNIT

#include "V3VhdlNativeAst.h"

#include <cstdint>
#include <string>

namespace vhdlnative {

//! Map one VHDL logic value character to the 2-state model: '0', '1', 'x', 'z', or 0
char vhdlLogicBit(char c) {
    switch (c) {
    case '0':
    case 'L':
    case 'l': return '0';
    case '1':
    case 'H':
    case 'h': return '1';
    case 'X':
    case 'x':
    case 'U':
    case 'u':
    case 'W':
    case 'w':
    case '-': return 'x';
    case 'Z':
    case 'z': return 'z';
    default: return 0;
    }
}

bool Lexer::bitsFromDigits(const std::string& digits, int base, std::string& out) {
    out.clear();
    for (const char c : digits) {
        if (c == '_') continue;
        // '-' is a don't-care position: keep it so matching operators can mask it
        const char bit = c == '-' ? '-' : vhdlLogicBit(c);
        int value = -1;
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        }
        if (bit && (value < 0 || value >= base)) {
            // 'X'/'Z'/'-' digits replicate the unknown/high-Z value
            for (int i = 0; i < bitsPerDigit(base); ++i) out.push_back(bit);
            continue;
        }
        if (value < 0 || value >= base) return false;
        for (int bitn = bitsPerDigit(base) - 1; bitn >= 0; --bitn) {
            out.push_back(((value >> bitn) & 1) ? '1' : '0');
        }
    }
    return true;
}
//! Convert a decimal digit string to an integer, skipping underscores
bool Lexer::decimalValue(const std::string& digits, int64_t& out) {
    int64_t value = 0;
    bool any = false;
    for (const char c : digits) {
        if (c == '_') continue;
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        value = value * 10 + (c - '0');
        any = true;
    }
    out = value;
    return any;
}

void Lexer::skipSpace() {
    for (;;) {
        if (m_pos >= m_text.size()) return;
        const char c = m_text[m_pos];
        if (c == '\n') {
            ++m_line;
            ++m_pos;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            ++m_pos;
        } else if (c == '-' && peek(1) == '-') {
            while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
        } else if (c == '/' && peek(1) == '*') {
            m_pos += 2;
            while (m_pos < m_text.size() && !(m_text[m_pos] == '*' && peek(1) == '/')) {
                if (m_text[m_pos] == '\n') ++m_line;
                ++m_pos;
            }
            if (m_pos < m_text.size()) m_pos += 2;
        } else {
            return;
        }
    }
}
Token Lexer::lexSymbol(FileLine* flp) {
    static const char* const threech[] = {"?/=", "?<=", "?>="};
    static const char* const twoch[]
        = {"<=", ">=", ":=", "=>", "/=", "**", "<>", "?=", "?<", "?>", "<<", ">>", "??"};
    Token tok;
    tok.kind = Token::SYM;
    tok.flp = flp;
    for (const char* op : threech) {
        if (m_text.compare(m_pos, 3, op) == 0) {
            m_pos += 3;
            tok.text = op;
            tok.sym = op[0];
            return tok;
        }
    }
    for (const char* op : twoch) {
        if (m_text.compare(m_pos, 2, op) == 0) {
            m_pos += 2;
            tok.text = op;
            tok.sym = op[0];
            return tok;
        }
    }
    tok.sym = m_text[m_pos];
    tok.text = std::string(1, tok.sym);
    ++m_pos;
    return tok;
}
Token Lexer::lexBits(FileLine* flp, const std::string& baseText) {
    // Caller has verified the next character is '"'
    const int base = baseText == "b" ? 2 : baseText == "o" ? 8 : baseText == "d" ? 10 : 16;
    ++m_pos;  // Opening quote
    std::string digits;
    while (m_pos < m_text.size() && m_text[m_pos] != '"' && m_text[m_pos] != '\n') {
        digits.push_back(m_text[m_pos]);
        ++m_pos;
    }
    Token tok;
    tok.kind = Token::BITS;
    tok.flp = flp;
    if (m_pos < m_text.size() && m_text[m_pos] == '"') {
        ++m_pos;
    } else {
        flp->v3error("VHDL bit-string literal is missing its closing '\"'");
        tok.kind = Token::END;
        return tok;
    }
    if (base == 10) {
        unsupportedError(flp, "decimal bit-string literal");
        tok.kind = Token::END;
        return tok;
    }
    if (!bitsFromDigits(digits, base, tok.bits)) {
        flp->v3error("Malformed VHDL bit-string literal");
        tok.kind = Token::END;
    }
    return tok;
}

Token Lexer::nextRaw() {
    skipSpace();
    Token tok;
    tok.flp = newFileLine();
    if (m_pos >= m_text.size()) {
        tok.kind = Token::END;
        return tok;
    }

    const char c = m_text[m_pos];
    if (isIdStart(c)) {
        const size_t start = m_pos;
        while (m_pos < m_text.size() && isIdChar(m_text[m_pos])) ++m_pos;
        tok.kind = Token::ID;
        tok.spelling = m_text.substr(start, m_pos - start);
        tok.text = tok.spelling;
        // VHDL identifiers are case-insensitive; lookup uses the folded form
        std::transform(tok.text.begin(), tok.text.end(), tok.text.begin(), [](char ch) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        });
        // A one-letter base prefix directly followed by '"' is a bit-string literal
        if (tok.text.size() == 1
            && (tok.text == "b" || tok.text == "o" || tok.text == "x" || tok.text == "d")
            && peek(0) == '"') {
            return lexBits(tok.flp, tok.text);
        }
        return tok;
    }
    if (c == '\\') {
        // Extended identifier: \name\ is case-sensitive and a backslash inside it is
        // doubled. Keep the raw \...\ spelling as the identity so an extended
        // identifier cannot collide with a simple name made of the same letters.
        const size_t start = m_pos;
        ++m_pos;  // The opening backslash
        bool closed = false;
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos];
            if (ch == '\n') break;
            if (ch == '\\') {
                if (m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '\\') {
                    m_pos += 2;  // An escaped backslash stays inside the identifier
                    continue;
                }
                ++m_pos;  // The closing backslash
                closed = true;
                break;
            }
            ++m_pos;
        }
        if (!closed) {
            tok.flp->v3error("VHDL extended identifier is missing its closing '\\'");
            tok.kind = Token::END;
            return tok;
        }
        tok.kind = Token::ID;
        tok.spelling = m_text.substr(start, m_pos - start);
        tok.text = tok.spelling;
        return tok;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        const size_t start = m_pos;
        while (
            m_pos < m_text.size()
            && (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '_')) {
            ++m_pos;
        }
        const std::string first = m_text.substr(start, m_pos - start);
        // A real literal with an exponent but no decimal point, e.g. 25e6
        if (peek(0) == 'e' || peek(0) == 'E') {
            const bool sign = peek(1) == '+' || peek(1) == '-';
            const char after = sign ? peek(2) : peek(1);
            if (std::isdigit(static_cast<unsigned char>(after))) {
                m_pos += 1 + (sign ? 1 : 0);
                while (m_pos < m_text.size()
                       && (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))
                           || m_text[m_pos] == '_')) {
                    ++m_pos;
                }
                std::string clean;
                for (size_t i = start; i < m_pos; ++i) {
                    if (m_text[i] != '_') clean.push_back(m_text[i]);
                }
                tok.kind = Token::REAL;
                tok.realValue = std::stod(clean);
                tok.text = clean;
                return tok;
            }
        }
        if (peek(0) == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            ++m_pos;  // The '.'
            while (m_pos < m_text.size()
                   && (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))
                       || m_text[m_pos] == '_')) {
                ++m_pos;
            }
            if (peek(0) == 'e' || peek(0) == 'E') {
                const bool sign = peek(1) == '+' || peek(1) == '-';
                const char after = sign ? peek(2) : peek(1);
                if (std::isdigit(static_cast<unsigned char>(after))) {
                    m_pos += 1 + (sign ? 1 : 0);
                    while (m_pos < m_text.size()
                           && (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))
                               || m_text[m_pos] == '_')) {
                        ++m_pos;
                    }
                }
            }
            std::string clean;
            for (size_t i = start; i < m_pos; ++i) {
                if (m_text[i] != '_') clean.push_back(m_text[i]);
            }
            tok.kind = Token::REAL;
            tok.realValue = std::stod(clean);
            tok.text = clean;
            return tok;
        }
        int64_t value = 0;
        if (peek(0) == '#') {
            int64_t base = 0;
            if (!decimalValue(first, base) || base < 2 || base > 16) {
                tok.flp->v3error("Malformed VHDL based literal");
                tok.kind = Token::END;
                return tok;
            }
            ++m_pos;  // '#'
            const size_t dstart = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != '#' && m_text[m_pos] != '\n') {
                ++m_pos;
            }
            if (m_pos >= m_text.size() || m_text[m_pos] != '#') {
                tok.flp->v3error("VHDL based literal is missing its closing '#'");
                tok.kind = Token::END;
                return tok;
            }
            const std::string digits = m_text.substr(dstart, m_pos - dstart);
            ++m_pos;  // '#'
            int64_t result = 0;
            for (const char d : digits) {
                if (d == '_') continue;
                int digit = -1;
                if (d >= '0' && d <= '9')
                    digit = d - '0';
                else if (d >= 'a' && d <= 'f')
                    digit = d - 'a' + 10;
                else if (d >= 'A' && d <= 'F')
                    digit = d - 'A' + 10;
                if (digit < 0 || digit >= base) {
                    tok.flp->v3error("Malformed digit in VHDL based literal");
                    tok.kind = Token::END;
                    return tok;
                }
                result = result * base + digit;
            }
            value = result;
        } else if (!decimalValue(first, value)) {
            tok.flp->v3error("Malformed VHDL integer literal");
            tok.kind = Token::END;
            return tok;
        }
        tok.kind = Token::INT;
        tok.intValue = value;
        tok.text = first;
        return tok;
    }
    if (c == '\'') {
        // Character literal ('0'); anything else is an attribute or qualification
        // mark. A mark immediately followed by '(' after a name, e.g. the `'('` in
        // std_logic'('1'), is a qualified expression, not the character literal '('.
        if (peek(1) != '\'' && peek(1) != '\n' && peek(2) == '\''
            && !(peek(1) == '(' && m_prevEndsDesignator)) {
            tok.kind = Token::CHAR;
            tok.charValue = peek(1);
            m_pos += 3;
            return tok;
        }
        tok.kind = Token::SYM;
        tok.sym = '\'';
        tok.text = "'";
        ++m_pos;
        return tok;
    }
    if (c == '"') {
        ++m_pos;
        const size_t start = m_pos;
        while (m_pos < m_text.size() && m_text[m_pos] != '"' && m_text[m_pos] != '\n') { ++m_pos; }
        if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
            tok.flp->v3error("VHDL string literal is missing its closing '\"'");
            tok.kind = Token::END;
            return tok;
        }
        tok.kind = Token::STRING;
        tok.text = m_text.substr(start, m_pos - start);
        ++m_pos;
        return tok;
    }
    if (c == '(' || c == ')' || c == ';' || c == ':' || c == ',' || c == '.' || c == '='
        || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' || c == '&'
        || c == '|' || c == '#' || c == '?' || c == '{' || c == '}' || c == '[' || c == ']') {
        return lexSymbol(tok.flp);
    }
    unsupportedError(tok.flp, std::string{"character '"} + c + "'");
    ++m_pos;
    tok.kind = Token::END;
    return tok;
}

Token Lexer::next() {
    // Remember whether the token just handed back can precede an attribute or
    // qualified-expression apostrophe, so nextRaw() can disambiguate the `'` above.
    Token tok = nextRaw();
    m_prevEndsDesignator = tok.kind == Token::ID || tok.kind == Token::CHAR
                           || tok.kind == Token::STRING
                           || (tok.kind == Token::SYM && (tok.sym == ')' || tok.sym == ']'));
    return tok;
}
}  // namespace vhdlnative
