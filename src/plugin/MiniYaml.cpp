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
// MiniYaml — implementation. See MiniYaml.h for the supported subset.
#include "MiniYaml.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace cursestone::plugin::yaml {

namespace {

// ---------------------------------------------------------------------------
// Utilidades de texto
// ---------------------------------------------------------------------------
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// Strips a line comment: '#' at line start or preceded by whitespace,
// but NEVER inside quotes.
std::string stripComment(const std::string& in) {
    bool inDq = false, inSq = false;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (inDq) {
            if (c == '"')
                inDq = false;
        } else if (inSq) {
            if (c == '\'')
                inSq = false;
        } else if (c == '"')
            inDq = true;
        else if (c == '\'')
            inSq = true;
        else if (c == '#' && (i == 0 || in[i - 1] == ' ' || in[i - 1] == '\t'))
            return in.substr(0, i);
    }
    return in;
}

// Quita comillas de un string YAML: "..." (con escapes) o '...' ('' -> ').
bool unquote(const std::string& raw, std::string& out, std::string& err) {
    char q = raw[0];
    if (raw.size() < 2 || raw.back() != q) {
        err = "unclosed quotes: " + raw;
        return false;
    }
    std::string body = raw.substr(1, raw.size() - 2);
    if (q == '\'') {
        // YAML single: '' dentro de comillas simples es una comilla literal.
        out.clear();
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\'' && i + 1 < body.size() && body[i + 1] == '\'') {
                out += '\'';
                ++i;
            } else
                out += body[i];
        }
        return true;
    }
    // Doble: escapes estilo JSON (\n \t \\ \" \r \0 y \uXXXX -> UTF-8).
    out.clear();
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i >= body.size()) {
            err = "incomplete escape in " + raw;
            return false;
        }
        char e = body[i];
        switch (e) {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case '0':
            out += '\0';
            break;
        case '\\':
            out += '\\';
            break;
        case '"':
            out += '"';
            break;
        case '/':
            out += '/';
            break;
        case 'u': {
            if (i + 4 >= body.size()) {
                err = "incomplete \\u in " + raw;
                return false;
            }
            unsigned cp = 0;
            for (int k = 1; k <= 4; ++k) {
                char h = body[i + k];
                cp <<= 4;
                if (h >= '0' && h <= '9')
                    cp += (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f')
                    cp += (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    cp += (unsigned)(h - 'A' + 10);
                else {
                    err = "invalid digit in \\u in " + raw;
                    return false;
                }
            }
            i += 4;
            if (cp < 0x80)
                out += (char)cp;
            else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default:
            err = std::string("unknown escape '\\") + e + "' in " + raw;
            return false;
        }
    }
    return true;
}

// Is it a YAML number (int or float, base 10)? Pushes the number.
bool scalarToNumber(lua_State* L, const std::string& s) {
    if (s.empty())
        return false;
    char* end = nullptr;
    long long iv = strtoll(s.c_str(), &end, 10);
    if (end && *end == '\0' && end != s.c_str()) {
        lua_pushinteger(L, (lua_Integer)iv);
        return true;
    }
    end = nullptr;
    double dv = strtod(s.c_str(), &end);
    if (end && *end == '\0' && end != s.c_str()) {
        lua_pushnumber(L, dv);
        return true;
    }
    return false;
}

// Parsea un escalar YAML y apila su valor Lua. `raw` ya viene recortado y
// without comment. `pushedNil` reports a nil value (null/~/empty).
bool parseScalar(lua_State* L, const std::string& raw, bool& pushedNil, std::string& err) {
    pushedNil = false;
    if (raw.empty()) {
        pushedNil = true;
        lua_pushnil(L);
        return true;
    }
    if (raw[0] == '"' || raw[0] == '\'') {
        std::string s, e2;
        if (!unquote(raw, s, e2)) {
            err = e2;
            return false;
        }
        lua_pushlstring(L, s.data(), s.size());
        return true;
    }
    if (raw == "{}" || raw == "[]") {
        lua_newtable(L);
        return true;
    }
    if (raw == "true" || raw == "True" || raw == "TRUE") {
        lua_pushboolean(L, 1);
        return true;
    }
    if (raw == "false" || raw == "False" || raw == "FALSE") {
        lua_pushboolean(L, 0);
        return true;
    }
    if (raw == "null" || raw == "Null" || raw == "NULL" || raw == "~") {
        pushedNil = true;
        lua_pushnil(L);
        return true;
    }
    if (scalarToNumber(L, raw))
        return true;
    lua_pushlstring(L, raw.data(), raw.size()); // string pelado
    return true;
}

// ---------------------------------------------------------------------------
// Indentation-based block parser
// ---------------------------------------------------------------------------
struct YLine {
    int indent;
    std::string text;
};

bool splitLines(const std::string& text, std::vector<YLine>& out, std::string& err) {
    size_t pos = 0, lineNo = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        ++lineNo;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        line = stripComment(line);
        if (trim(line).empty())
            continue;

        int indent = 0;
        while (indent < (int)line.size() && line[indent] == ' ')
            ++indent;
        if (indent < (int)line.size() && line[indent] == '\t') {
            err = "tab in indentation (line " + std::to_string(lineNo) + ") — use spaces";
            return false;
        }
        out.push_back(YLine{indent, trim(line)});
    }
    return true;
}

// Localiza los ':' que separan "clave: valor" (respetando comillas; YAML
// requires whitespace or end of line after mapping ':'s).
bool findKeyColon(const std::string& s, size_t& colon) {
    bool inDq = false, inSq = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inDq) {
            if (c == '"')
                inDq = false;
        } else if (inSq) {
            if (c == '\'')
                inSq = false;
        } else if (c == '"')
            inDq = true;
        else if (c == '\'')
            inSq = true;
        else if (c == ':') {
            if (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t') {
                colon = i;
                return true;
            }
        }
    }
    return false;
}

bool isListLine(const std::string& text) {
    return text == "-" || text.rfind("- ", 0) == 0;
}

// Parsea el bloque que empieza en lines[idx] al nivel `indent` y apila UNA
// table (map or list) on the stack. Advances idx past the block.
bool parseBlock(lua_State* L, std::vector<YLine>& lines, size_t& idx, int indent,
                std::string& err) {
    bool isList = isListLine(lines[idx].text);
    lua_newtable(L); // [..., tabla]
    int t = lua_gettop(L);
    lua_Integer n = 0;

    while (idx < lines.size()) {
        YLine ln = lines[idx];
        if (ln.indent < indent)
            break;
        if (ln.indent > indent) {
            err = "unexpected indentation at '" + ln.text + "'";
            return false;
        }
        if (isListLine(ln.text) != isList) {
            err = "cannot mix list (-) and map at the same level: '" + ln.text + "'";
            return false;
        }

        if (isList) {
            std::string rest = (ln.text == "-") ? "" : trim(ln.text.substr(2));
            size_t colon;
            if (rest.empty()) {
                // bare "-": the value is a deeper nested block.
                ++idx;
                if (idx >= lines.size() || lines[idx].indent <= indent) {
                    err = "list item '-' with no content";
                    return false;
                }
                if (!parseBlock(L, lines, idx, lines[idx].indent, err))
                    return false;
            } else if (rest[0] != '"' && rest[0] != '\'' && rest != "{}" && rest != "[]" &&
                       (isListLine(rest) || findKeyColon(rest, colon))) {
                // "- clave: valor" o "- - item" (mapa / sub-lista inline):
                // rewrite the line as a block at the inner column and
                // reuse parseBlock (that is how the following keys
                // alineadas tras el "- ").
                lines[idx] = YLine{indent + 2, rest};
                if (!parseBlock(L, lines, idx, indent + 2, err))
                    return false;
            } else {
                bool pushedNil;
                if (!parseScalar(L, rest, pushedNil, err)) {
                    err = "invalid scalar in list '" + rest + "': " + err;
                    return false;
                }
                ++idx;
                if (pushedNil)
                    lua_pop(L, 1), lua_pushnil(L);
            }
            lua_rawseti(L, t, (lua_Integer)++n);
            continue;
        }

        // --- Entrada de mapa: "clave: valor" / "clave:" + bloque anidado ---
        size_t colon;
        if (!findKeyColon(ln.text, colon)) {
            err = "expected 'key: value' but found '" + ln.text + "'";
            return false;
        }
        std::string keyRaw = trim(ln.text.substr(0, colon));
        std::string valPart = trim(ln.text.substr(colon + 1));

        std::string key;
        if (!keyRaw.empty() && (keyRaw[0] == '"' || keyRaw[0] == '\'')) {
            std::string e2;
            if (!unquote(keyRaw, key, e2)) {
                err = "invalid key '" + keyRaw + "': " + e2;
                return false;
            }
        } else {
            key = keyRaw;
        }

        if (valPart.empty()) {
            // Value on the following lines (or empty -> the key is skipped).
            ++idx;
            if (idx < lines.size() && lines[idx].indent > indent) {
                if (!parseBlock(L, lines, idx, lines[idx].indent, err))
                    return false;
            } else {
                lua_pushnil(L);
            }
        } else {
            bool pushedNil;
            if (!parseScalar(L, valPart, pushedNil, err)) {
                err = "invalid value for '" + key + "': " + err;
                return false;
            }
            ++idx;
        }

        if (!lua_isnil(L, -1))
            lua_setfield(L, t, key.c_str());
        else
            lua_pop(L, 1); // nil: no guardar la clave
    }
    return true;
}

// ---------------------------------------------------------------------------
// Serializador: tabla Lua -> YAML
// ---------------------------------------------------------------------------
// Normalizes a stack index (possibly relative) to ABSOLUTE. CRITICAL:
// these functions push values while working; a stored -1 would stop
// apuntar al valor original tras el primer push (y lua_next sobre la pila
// desplazada = crash). Normalizar al entrar evita toda la clase de bug.
inline int absIndex(lua_State* L, int idx) {
    return (idx < 0) ? lua_gettop(L) + 1 + idx : idx;
}

// Is this a "pure array" table: keys exactly 1..n without gaps or extras?
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
    // Verificar que no haya claves extra fuera de 1..n.
    lua_Integer extras = 0;
    lua_pushnil(L);
    while (lua_next(L, abs)) {
        ++extras;
        lua_pop(L, 1);
    }
    return extras == n;
}

bool tableIsEmpty(lua_State* L, int idx) {
    int abs = absIndex(L, idx); // antes de apilar nada
    lua_pushnil(L);
    bool empty = (lua_next(L, abs) == 0);
    if (!empty)
        lua_pop(L, 2); // clave + valor
    return empty;
}

bool safeBare(const std::string& s) {
    if (s.empty())
        return false;
    if (!(std::isalnum((unsigned char)s[0]) || s[0] == '_'))
        return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-' || c == '/'))
            return false;
    // Do not confuse with booleans / null / numbers.
    if (s == "true" || s == "false" || s == "null" || s == "~" || s == "True" || s == "False" ||
        s == "Null" || s == "NULL")
        return false;
    char* end = nullptr;
    strtod(s.c_str(), &end);
    if (end && *end == '\0' && end != s.c_str())
        return false;
    return true;
}

std::string quoteDouble(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
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

// YAML representation of a Lua scalar.
std::string scalarRepr(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx) ? "true" : "false";
    case LUA_TNUMBER: {
        if (lua_isinteger(L, idx)) {
            char buf[32];
            snprintf(buf, sizeof buf, "%lld", (long long)lua_tointeger(L, idx));
            return buf;
        }
        double d = lua_tonumber(L, idx);
        if (std::isfinite(d) && std::fabs(d) < 1e15 && d == std::floor(d)) {
            char buf[32];
            snprintf(buf, sizeof buf, "%.0f", d);
            return buf;
        }
        char buf[40];
        snprintf(buf, sizeof buf, "%.14g", d);
        return buf;
    }
    case LUA_TSTRING: {
        std::string s = lua_tostring(L, idx);
        return safeBare(s) ? s : quoteDouble(s);
    }
    default:
        return "null";
    }
}

// Mutual forward declaration.
bool emitValue(lua_State* L, int abs, int indent, std::string& out, std::string& err);

// Emits a table in list mode. `abs` = the table's index (normalized).
bool emitList(lua_State* L, int idx, lua_Integer n, int indent, std::string& out,
              std::string& err) {
    int abs = absIndex(L, idx);
    std::string pad(indent, ' ');
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, abs, i);
        if (lua_istable(L, -1)) {
            if (tableIsEmpty(L, -1)) {
                out += pad + "- {}\n";
                lua_pop(L, 1);
                continue;
            }
            // Cualquier tabla (mapa o sub-lista): emitir el bloque a
            // indent+2 and "glue" the dash onto the first line:
            //   body  = "    k: v\n    k2: v2\n"      (indent+2 = 4)
            //   out  += "  -" + body.substr(3)  ->  "  - k: v\n    k2: v2\n"
            std::string body;
            if (!emitValue(L, -1, indent + 2, body, err))
                return false;
            lua_pop(L, 1);
            if (body.empty()) {
                out += pad + "- {}\n";
                continue;
            }
            out += pad + "-" + body.substr(indent + 1);
        } else {
            out += pad + "- " + scalarRepr(L, -1) + "\n";
            lua_pop(L, 1);
        }
    }
    return true;
}

// Emite un valor Lua (tabla o escalar) con el indent dado.
bool emitValue(lua_State* L, int idx, int indent, std::string& out, std::string& err) {
    int abs = absIndex(L, idx); // normalizar ANTES de cualquier push
    if (!lua_istable(L, abs)) {
        out = scalarRepr(L, abs) + "\n";
        return true;
    }
    lua_Integer n;
    if (isArrayTable(L, abs, n))
        return emitList(L, abs, n, indent, out, err);

    std::string pad(indent, ' ');
    lua_pushnil(L);
    while (lua_next(L, abs)) {
        // -2 clave, -1 valor
        if (lua_type(L, -2) != LUA_TSTRING) {
            err = "non-string key in the table (use strings as keys)";
            lua_pop(L, 2);
            return false;
        }
        std::string key = lua_tostring(L, -2);
        std::string keyRepr = safeBare(key) ? key : quoteDouble(key);

        if (lua_istable(L, -1)) {
            if (tableIsEmpty(L, -1)) {
                out += pad + keyRepr + ": {}\n";
                lua_pop(L, 1);
                continue;
            }
            out += pad + keyRepr + ":\n";
            if (!emitValue(L, -1, indent + 2, out, err))
                return false;
            lua_pop(L, 1);
        } else {
            out += pad + keyRepr + ": " + scalarRepr(L, -1) + "\n";
            lua_pop(L, 1);
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool parseToLua(lua_State* L, const std::string& text, std::string& err) {
    std::vector<YLine> lines;
    if (!splitLines(text, lines, err))
        return false;
    if (lines.empty()) { // empty document == empty table
        lua_newtable(L);
        return true;
    }

    // One-line scalar document (neither map nor list).
    size_t colon;
    if (!isListLine(lines[0].text) && !findKeyColon(lines[0].text, colon)) {
        if (lines.size() > 1) {
            err = "expected 'key: value' but found '" + lines[0].text + "'";
            return false;
        }
        bool pushedNil;
        return parseScalar(L, lines[0].text, pushedNil, err);
    }

    size_t idx = 0;
    if (!parseBlock(L, lines, idx, lines[0].indent, err))
        return false;
    if (idx != lines.size()) {
        err = "loose lines at the end of the document (inconsistent indentation)";
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool fromLua(lua_State* L, int idx, std::string& out, std::string& err) {
    int abs = (idx < 0) ? lua_gettop(L) + 1 + idx : idx;
    return emitValue(L, abs, 0, out, err);
}

} // namespace cursestone::plugin::yaml
