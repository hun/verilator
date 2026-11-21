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
// SPDX-FileCopyrightText: 2026-2026 Wilson Snyder
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

#include <limits>
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
//! Convert a decimal digit string to an integer, skipping underscores; false if it is
//! not a decimal number or does not fit
bool Lexer::decimalValue(const std::string& digits, int64_t& out) {
    int64_t value = 0;
    bool any = false;
    for (const char c : digits) {
        if (c == '_') continue;
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        const int digit = c - '0';
        if (value > (std::numeric_limits<int64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
        any = true;
    }
    out = value;
    return any;
}

void Lexer::skipSpace() {
    while (true) {
        if (m_pos >= m_text.size()) return;
        const char c = m_text[m_pos];
        if (c == '\n') {
            ++m_line;
            ++m_pos;
            m_lineStart = m_pos;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            ++m_pos;
        } else if (c == '-' && peek(1) == '-') {
            const size_t start = m_pos + 2;  // Past the comment mark
            while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
            notePragma(pragmaDirection(start, m_pos));
        } else if (c == '/' && peek(1) == '*') {
            m_pos += 2;
            const size_t start = m_pos;
            while (m_pos < m_text.size() && !(m_text[m_pos] == '*' && peek(1) == '/')) {
                if (m_text[m_pos] == '\n') {
                    ++m_line;
                    m_lineStart = m_pos + 1;
                }
                ++m_pos;
            }
            const size_t end = m_pos;
            if (m_pos < m_text.size()) m_pos += 2;
            notePragma(pragmaDirection(start, end));
        } else {
            return;
        }
    }
}

//! Comment pragma named by the given span, which excludes the comment marks: +1 for
//! translate_off, -1 for translate_on, 0 for any other comment. A tool writes the pragma
//! with its own prefix (synopsys, synthesis, pragma), so the pragma word is what ends a
//! directive comment -- prose that only mentions it names no region.
int Lexer::pragmaDirection(size_t start, size_t end) const {
    size_t at = start;
    std::string lastWord;
    while (true) {
        while (at < end && std::isspace(static_cast<unsigned char>(m_text[at]))) ++at;
        if (at >= end) break;
        std::string word;
        while (at < end && (isIdChar(m_text[at]) || m_text[at] == '!')) {
            word.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(m_text[at]))));
            ++at;
        }
        if (word.empty()) return 0;  // Punctuation: not a directive
        lastWord = word;
    }
    if (lastWord == "translate_off") return 1;
    if (lastWord == "translate_on") return -1;
    return 0;
}

void Lexer::notePragma(const int direction) {
    if (direction > 0) {
        ++m_translateOff;
    } else if (direction < 0 && m_translateOff > 0) {
        // A translate_on without its translate_off closes nothing
        --m_translateOff;
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
Token Lexer::lexBits(FileLine* flp, const std::string& baseText, int length) {
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
        flp->v3error("VHDL bit-string literal is missing its closing '\"'.");
        tok.kind = Token::END;
        return tok;
    }
    if (base == 10) {
        unsupportedError(flp, "decimal bit-string literal");
        tok.kind = Token::END;
        return tok;
    }
    if (!bitsFromDigits(digits, base, tok.bits)) {
        flp->v3error("Malformed VHDL bit-string literal.");
        tok.kind = Token::END;
        return tok;
    }
    // The length is part of the literal's value: zero padding is free, dropping a set bit
    // would change it, which is the truncation the standard forbids
    if (length >= 0) {
        const int excess = static_cast<int>(tok.bits.size()) - length;
        if (excess > 0) {
            for (int i = 0; i < excess; ++i) {
                if (tok.bits[i] != '0') {
                    flp->v3error("VHDL bit-string literal does not fit its length.");
                    break;
                }
            }
            tok.bits.erase(0, excess);
        } else if (excess < 0) {
            tok.bits.insert(0, -excess, '0');
        }
    }
    return tok;
}

Token Lexer::nextRaw() {
    Token tok = scanToken();
    // A translate_off region is not part of the design, so its tokens are dropped; a region
    // left open runs to the end of the file
    while (m_translateOff > 0 && tok.kind != Token::END) tok = scanToken();
    return tok;
}

void Lexer::skipDigits() {
    while (m_pos < m_text.size()
           && (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '_')) {
        ++m_pos;
    }
}

bool Lexer::consumeExponent() {
    if (peek(0) != 'e' && peek(0) != 'E') return false;
    const bool sign = peek(1) == '+' || peek(1) == '-';
    const char after = sign ? peek(2) : peek(1);
    if (!std::isdigit(static_cast<unsigned char>(after))) return false;
    m_pos += 1 + (sign ? 1 : 0);
    skipDigits();
    return true;
}

Token Lexer::makeRealToken(FileLine* flp, size_t start) {
    std::string clean;
    for (size_t i = start; i < m_pos; ++i) {
        if (m_text[i] != '_') clean.push_back(m_text[i]);
    }
    Token tok;
    tok.kind = Token::REAL;
    tok.flp = flp;
    tok.realValue = std::stod(clean);
    tok.text = clean;
    return tok;
}

Token Lexer::lexIdentifier(FileLine* flp) {
    Token tok;
    tok.flp = flp;
    const size_t start = m_pos;
    while (m_pos < m_text.size() && isIdChar(m_text[m_pos])) ++m_pos;
    tok.kind = Token::ID;
    tok.spelling = m_text.substr(start, m_pos - start);
    tok.text = tok.spelling;
    // IEEE 1076-2008 15.4.2: a basic identifier starts with a letter, never has
    // two underscores in a row and never ends with one
    if (tok.spelling.front() == '_') {
        tok.flp->v3error("VHDL identifier cannot begin with an underscore: '" + tok.spelling
                         + "'.");
    } else if (tok.spelling.back() == '_') {
        tok.flp->v3error("VHDL identifier cannot end with an underscore: '" + tok.spelling + "'.");
    } else if (tok.spelling.find("__") != std::string::npos) {
        tok.flp->v3error("VHDL identifier cannot contain two underscores in a row: '"
                         + tok.spelling + "'.");
    }
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

Token Lexer::lexExtendedIdentifier(FileLine* flp) {
    Token tok;
    tok.flp = flp;
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
        tok.flp->v3error("VHDL extended identifier is missing its closing backslash.");
        tok.kind = Token::END;
        return tok;
    }
    tok.kind = Token::ID;
    tok.spelling = m_text.substr(start, m_pos - start);
    tok.text = tok.spelling;
    return tok;
}

Token Lexer::lexIntegerLiteral(FileLine* flp, const std::string& first) {
    Token tok;
    tok.flp = flp;
    if (peek(0) == '#') return lexBasedLiteral(flp, first);
    int64_t value = 0;
    if (!decimalValue(first, value)) {
        tok.flp->v3error("VHDL integer literal that overflows.");
        tok.kind = Token::END;
        return tok;
    }
    tok.kind = Token::INT;
    tok.intValue = value;
    tok.text = first;
    return tok;
}

Token Lexer::lexBasedLiteral(FileLine* flp, const std::string& first) {
    Token tok;
    tok.flp = flp;
    int64_t base = 0;
    if (!decimalValue(first, base) || base < 2 || base > 16) {
        tok.flp->v3error("Malformed VHDL based literal.");
        tok.kind = Token::END;
        return tok;
    }
    ++m_pos;  // '#'
    const size_t dstart = m_pos;
    while (m_pos < m_text.size() && m_text[m_pos] != '#' && m_text[m_pos] != '\n') { ++m_pos; }
    if (m_pos >= m_text.size() || m_text[m_pos] != '#') {
        tok.flp->v3error("VHDL based literal is missing its closing '#'.");
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
            tok.flp->v3error("Malformed digit in VHDL based literal.");
            tok.kind = Token::END;
            return tok;
        }
        result = result * base + digit;
    }
    int64_t value = result;
    // A based literal may carry an exponent (IEEE 1076-2008 15.5):
    // 16#FF#E2 is 255 * 16**2
    if (peek(0) == 'e' || peek(0) == 'E') {
        const bool negExp = peek(1) == '-';
        const bool sign = negExp || peek(1) == '+';
        const char after = sign ? peek(2) : peek(1);
        if (std::isdigit(static_cast<unsigned char>(after))) {
            if (negExp) {
                tok.flp->v3error("VHDL based literal with a negative exponent.");
                tok.kind = Token::END;
                return tok;
            }
            m_pos += 1 + (sign ? 1 : 0);
            int64_t exp = 0;
            while (m_pos < m_text.size()
                   && (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))
                       || m_text[m_pos] == '_')) {
                if (m_text[m_pos] != '_') exp = exp * 10 + (m_text[m_pos] - '0');
                ++m_pos;
            }
            for (int64_t i = 0; i < exp; ++i) {
                if (value > INT64_MAX / base) {
                    tok.flp->v3error("VHDL based literal that overflows.");
                    tok.kind = Token::END;
                    return tok;
                }
                value *= base;
            }
        }
    }
    tok.kind = Token::INT;
    tok.intValue = value;
    tok.text = first;
    return tok;
}

Token Lexer::lexNumber(FileLine* flp) {
    Token tok;
    tok.flp = flp;
    const size_t start = m_pos;
    skipDigits();
    const std::string first = m_text.substr(start, m_pos - start);
    // A real literal with an exponent but no decimal point, e.g. 25e6
    if (peek(0) == 'e' || peek(0) == 'E') {
        if (consumeExponent()) return makeRealToken(flp, start);
    }
    // A bit-string literal may state its length before the base, e.g. 6x"3f"
    const char baseChar = static_cast<char>(std::tolower(static_cast<unsigned char>(peek(0))));
    if ((baseChar == 'b' || baseChar == 'o' || baseChar == 'x' || baseChar == 'd')
        && peek(1) == '"') {
        ++m_pos;  // The base letter
        int64_t bitLength = 0;
        if (!decimalValue(first, bitLength) || bitLength > std::numeric_limits<int>::max()) {
            tok.flp->v3error("VHDL bit-string length that overflows.");
            tok.kind = Token::END;
            return tok;
        }
        return lexBits(tok.flp, std::string(1, baseChar), static_cast<int>(bitLength));
    }
    if (peek(0) == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        ++m_pos;  // The '.'
        skipDigits();
        if (peek(0) == 'e' || peek(0) == 'E') consumeExponent();
        return makeRealToken(flp, start);
    }
    return lexIntegerLiteral(flp, first);
}

Token Lexer::lexCharOrMark(FileLine* flp) {
    Token tok;
    tok.flp = flp;
    // Character literal ('0'); anything else is an attribute or qualification
    // mark. A mark immediately followed by '(' after a name, e.g. the `'('` in
    // std_logic'('1'), is a qualified expression, not the character literal '('.
    // A reserved word cannot name a mark (IEEE 1076-2008 15.10), so `'('` after
    // one is the character literal, as a case choice writes it (15.6)
    const bool qualifiedExpr
        = peek(1) == '(' && m_prevEndsDesignator && (!m_prevId || !isReservedWord(m_prevText));
    if (peek(1) != '\n' && peek(2) == '\'' && !qualifiedExpr) {
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

Token Lexer::lexStringLiteral(FileLine* flp) {
    Token tok;
    tok.flp = flp;
    ++m_pos;
    std::string text;
    while (true) {
        if (m_pos >= m_text.size() || m_text[m_pos] == '\n') {
            tok.flp->v3error("VHDL string literal is missing its closing '\"'.");
            tok.kind = Token::END;
            return tok;
        }
        if (m_text[m_pos] != '"') {
            text.push_back(m_text[m_pos++]);
            continue;
        }
        // A doubled quote inside a literal is one quote character, and only a lone
        // quote closes it (IEEE 1076-2008 15.7)
        if (m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '"') {
            text.push_back('"');
            m_pos += 2;
            continue;
        }
        ++m_pos;  // The closing quote
        break;
    }
    tok.kind = Token::STRING;
    tok.text = std::move(text);
    return tok;
}

Token Lexer::scanToken() {
    skipSpace();
    Token tok;
    tok.flp = newFileLine();
    if (m_pos >= m_text.size()) {
        tok.kind = Token::END;
        return tok;
    }

    const char c = m_text[m_pos];
    if (isIdStart(c)) return lexIdentifier(tok.flp);
    if (c == '\\') return lexExtendedIdentifier(tok.flp);
    if (std::isdigit(static_cast<unsigned char>(c))) return lexNumber(tok.flp);
    if (c == '\'') return lexCharOrMark(tok.flp);
    if (c == '"') return lexStringLiteral(tok.flp);
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
    m_prevId = tok.kind == Token::ID;
    m_prevText = tok.text;
    m_prevEndsDesignator = tok.kind == Token::ID || tok.kind == Token::CHAR
                           || tok.kind == Token::STRING
                           || (tok.kind == Token::SYM && (tok.sym == ')' || tok.sym == ']'));
    return tok;
}
}  // namespace vhdlnative
