/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// MiniJson — implementación. Ver MiniJson.h.
#include "MiniJson.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cursestone::plugin::json {

namespace {

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
struct Parser {
    const std::string& src;
    size_t pos = 0;
    std::string err;

    explicit Parser(const std::string& s) : src(s) {
    }

    void skipWs() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos;
            else
                break;
        }
    }
    bool fail(const std::string& msg) {
        err = msg + " (offset " + std::to_string(pos) + ")";
        return false;
    }
    bool peek(char& c) {
        skipWs();
        if (pos >= src.size())
            return false;
        c = src[pos];
        return true;
    }

    // Apila 4 bytes UTF-8 de un code point BMP; maneja surrogate pairs.
    bool utf8FromHex(unsigned first, std::string& out) {
        unsigned cp = first;
        if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate
            if (pos + 6 > src.size() || src[pos] != '\\' || src[pos + 1] != 'u') {
                return fail("lone high surrogate");
            }
            unsigned low = 0;
            for (int k = 2; k <= 5; ++k) {
                char h = src[pos + k];
                low <<= 4;
                if (h >= '0' && h <= '9')
                    low += (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f')
                    low += (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    low += (unsigned)(h - 'A' + 10);
                else
                    return fail("invalid \\u escape");
            }
            pos += 6;
            if (low < 0xDC00 || low > 0xDFFF)
                return fail("invalid low surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("low surrogate without high");
        }
        if (cp < 0x80)
            out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        return true;
    }

    bool parseString(std::string& out) {
        // pre: src[pos] == '"'
        ++pos;
        out.clear();
        while (pos < src.size()) {
            unsigned char c = (unsigned char)src[pos];
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c == '\\') {
                if (++pos >= src.size())
                    return fail("escape at end of string");
                char e = src[pos++];
                switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    if (pos + 4 > src.size())
                        return fail("incomplete \\u");
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = src[pos + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')
                            cp += (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            cp += (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            cp += (unsigned)(h - 'A' + 10);
                        else
                            return fail("invalid digit in \\u");
                    }
                    pos += 4;
                    if (!utf8FromHex(cp, out))
                        return false;
                    break;
                }
                default:
                    return fail(std::string("invalid escape '\\") + e + "'");
                }
            } else {
                out += (char)c;
                ++pos;
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber() {
        size_t start = pos;
        if (pos < src.size() && src[pos] == '-')
            ++pos;
        while (pos < src.size() &&
               ((src[pos] >= '0' && src[pos] <= '9') || src[pos] == '.' || src[pos] == 'e' ||
                src[pos] == 'E' || src[pos] == '+' || src[pos] == '-'))
            ++pos;
        std::string num = src.substr(start, pos - start);
        if (num.empty())
            return fail("expected a number");
        // ¿Entero sin punto ni exponente? -> lua_Integer (evita 1.0 feo).
        if (num.find('.') == std::string::npos && num.find('e') == std::string::npos &&
            num.find('E') == std::string::npos) {
            char* end = nullptr;
            long long v = strtoll(num.c_str(), &end, 10);
            if (end && *end == '\0') {
                lua_pushinteger(luaState, v);
                return true;
            }
        }
        char* end = nullptr;
        double d = strtod(num.c_str(), &end);
        if (!end || *end != '\0' || end == num.c_str()) {
            return fail("invalid number '" + num + "'");
        }
        lua_pushnumber(luaState, d);
        return true;
    }

    lua_State* luaState; // (declarado aquí para parseNumber)

    bool parseValue() {
        char c;
        if (!peek(c))
            return fail("expected a value");
        switch (c) {
        case '{':
            return parseObject();
        case '[':
            return parseArray();
        case '"': {
            std::string s;
            if (!parseString(s))
                return false;
            lua_pushlstring(luaState, s.data(), s.size());
            return true;
        }
        case 't':
            if (src.compare(pos, 4, "true") == 0) {
                pos += 4;
                lua_pushboolean(luaState, 1);
                return true;
            }
            return fail("invalid literal");
        case 'f':
            if (src.compare(pos, 5, "false") == 0) {
                pos += 5;
                lua_pushboolean(luaState, 0);
                return true;
            }
            return fail("invalid literal");
        case 'n':
            if (src.compare(pos, 4, "null") == 0) {
                pos += 4;
                lua_pushnil(luaState);
                return true;
            }
            return fail("invalid literal");
        default:
            return parseNumber();
        }
    }

    bool parseObject() {
        ++pos; // '{'
        lua_newtable(luaState);
        int t = lua_gettop(luaState);
        char c;
        if (!peek(c))
            return fail("unterminated object");
        if (c == '}') {
            ++pos;
            return true;
        }
        while (true) {
            if (!peek(c))
                return fail("expected a key");
            if (c != '"')
                return fail("object key must be a string");
            std::string key;
            if (!parseString(key))
                return false;
            if (!peek(c))
                return fail("expected ':'");
            if (c != ':')
                return fail("expected ':'");
            ++pos;
            if (!parseValue())
                return false;
            if (lua_isnil(luaState, -1))
                lua_pop(luaState, 1); // null: omitir
            else
                lua_setfield(luaState, t, key.c_str());
            if (!peek(c))
                return fail("expected ',' or '}'");
            if (c == ',') {
                ++pos;
                continue;
            }
            if (c == '}') {
                ++pos;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray() {
        ++pos; // '['
        lua_newtable(luaState);
        int t = lua_gettop(luaState);
        lua_Integer n = 0;
        char c;
        if (!peek(c))
            return fail("unterminated array");
        if (c == ']') {
            ++pos;
            return true;
        }
        while (true) {
            if (!parseValue())
                return false;
            lua_rawseti(luaState, t, (lua_Integer)++n);
            if (!peek(c))
                return fail("expected ',' or ']'");
            if (c == ',') {
                ++pos;
                continue;
            }
            if (c == ']') {
                ++pos;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }
};

// ---------------------------------------------------------------------------
// Serializador
// ---------------------------------------------------------------------------
bool isArrayTable(lua_State* L, int abs, lua_Integer& n) {
    n = (lua_Integer)lua_rawlen(L, abs);
    if (n == 0)
        return false;
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, abs, i);
        bool missing = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (missing)
            return false;
    }
    lua_Integer extras = 0;
    lua_pushnil(L);
    while (lua_next(L, abs)) {
        ++extras;
        lua_pop(L, 1);
    }
    return extras == n;
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)c);
                out += buf;
            } else
                out += c;
        }
    }
    out += '"';
    return out;
}

bool emitValue(lua_State* L, int idx, std::string& out, std::string& err) {
    // Normalizar a índice ABSOLUTO antes de apilar nada: esta función
    // recibe -1 en las llamadas recursivas y luego hace lua_pushnil /
    // lua_rawgeti — un -1 "congelado" dejaría de apuntar al valor.
    int abs = (idx < 0) ? lua_gettop(L) + 1 + idx : idx;
    switch (lua_type(L, abs)) {
    case LUA_TNIL:
        out += "null";
        return true;
    case LUA_TBOOLEAN:
        out += lua_toboolean(L, abs) ? "true" : "false";
        return true;
    case LUA_TNUMBER: {
        if (lua_isinteger(L, abs)) {
            char buf[32];
            snprintf(buf, sizeof buf, "%lld", (long long)lua_tointeger(L, abs));
            out += buf;
        } else {
            double d = lua_tonumber(L, abs);
            if (!std::isfinite(d)) {
                err = "non-finite number (inf/nan)";
                return false;
            }
            if (std::fabs(d) < 1e15 && d == std::floor(d)) {
                char buf[32];
                snprintf(buf, sizeof buf, "%.0f", d);
                out += buf;
            } else {
                char buf[40];
                snprintf(buf, sizeof buf, "%.17g", d);
                out += buf;
            }
        }
        return true;
    }
    case LUA_TSTRING:
        out += quote(lua_tostring(L, abs));
        return true;
    case LUA_TTABLE: {
        lua_Integer n;
        if (isArrayTable(L, abs, n)) {
            out += '[';
            for (lua_Integer i = 1; i <= n; ++i) {
                if (i > 1)
                    out += ',';
                lua_rawgeti(L, abs, i);
                if (!emitValue(L, lua_gettop(L), out, err))
                    return false;
                lua_pop(L, 1);
            }
            out += ']';
            return true;
        }
        out += '{';
        bool first = true;
        lua_pushnil(L);
        while (lua_next(L, abs)) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                err = "non-string key in the table (use strings as keys)";
                lua_pop(L, 2);
                return false;
            }
            if (!first)
                out += ',';
            first = false;
            out += quote(lua_tostring(L, -2));
            out += ':';
            if (!emitValue(L, -1, out, err)) {
                lua_pop(L, 2);
                return false;
            }
            lua_pop(L, 1);
        }
        out += '}';
        return true;
    }
    default:
        err = "value not serializable to JSON (" + std::string(lua_typename(L, lua_type(L, abs))) +
              ")";
        return false;
    }
}

} // namespace

bool parseToLua(lua_State* L, const std::string& text, std::string& err) {
    Parser p(text);
    p.luaState = L;
    if (!p.parseValue()) {
        err = p.err;
        return false;
    }
    p.skipWs();
    if (p.pos != text.size()) {
        err = "trailing garbage after JSON value (offset " + std::to_string(p.pos) + ")";
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool fromLua(lua_State* L, int idx, std::string& out, std::string& err) {
    int abs = (idx < 0) ? lua_gettop(L) + 1 + idx : idx;
    return emitValue(L, abs, out, err);
}

} // namespace cursestone::plugin::json
