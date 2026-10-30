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
// BindingsData — API de datos y configs para los plugins Lua.
//
// Cada plugin tiene SU carpeta: data_pl/<carpetaDelPlugin>/ (se
// crea al cargar el plugin). TODA esta API restringe las rutas a esa
// carpeta con sanitizeRelPath(): sin "..", sin rutas absolutas, sin
// backslashes — un plugin NO puede leer ni escribir fuera de su carpeta.
//
//   data.folder                  ruta de la carpeta de datos (string)
//   data.read(archivo)           contenido crudo (binario seguro) o nil+err
//   data.write(archivo, txt)     escribe crudo (crea/sobrescribe); ok|nil+err
//   data.exists(file)            exists?
//   data.delete(file)            deletes (false if it did not exist)
//   data.list()                  nombres de las entradas de la carpeta
//   data.loadYml(archivo)        parsea YAML -> tabla | nil+err
//   data.saveYml(archivo, tbl)   tabla -> YAML (sobrescribe); ok|nil+err
//   data.loadJson(archivo)       parsea JSON -> tabla | nil+err
//   data.saveJson(archivo, tbl)  tabla -> JSON; ok|nil+err
//
// Con read/write se guardan .dat binarios (el string Lua 5.4 es binario-
// seguro: puede contener \0). Con loadYml/saveYml/loadJson/saveJson se
// store configs and structured data with any extension.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "PathSafe.h"
#include "MiniYaml.h"
#include "MiniJson.h"
#include <filesystem>
#include <fstream>

namespace cursestone::plugin {

namespace {

namespace fs = std::filesystem;

// Resuelve <dataDir>/<rel> de forma segura. Error de Lua si la ruta es
// invalid (absolute, containing "..", a backslash, or empty).
std::string resolveDataPath(lua_State* L, int idx) {
    const char* req = luaL_checkstring(L, idx);
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        luaL_error(L, "data: invalid path '%s' (use paths relative to your data folder, no '..')",
                   req);
    }
    return LuaPlugin::from(L)->dataDir() + "/" + rel;
}

bool readWholeFile(const std::string& path, std::string& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "could not open '" + path + "'";
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        err = "read error on '" + path + "'";
        return false;
    }
    return true;
}

bool writeWholeFile(const std::string& path, const std::string& data, std::string& err) {
    // Subcarpetas dentro de data_pl: crear la ruta padre si falta
    // (the path was already sanitized, so it cannot escape dataDir).
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    ec.clear();
    std::ofstream outF(path, std::ios::binary | std::ios::trunc);
    if (!outF) {
        err = "could not open for writing '" + path + "'";
        return false;
    }
    outF.write(data.data(), (std::streamsize)data.size());
    if (!outF) {
        err = "write error on '" + path + "'";
        return false;
    }
    return true;
}

// data.read(archivo) -> contenido | nil, err
int l_data_read(lua_State* L) {
    std::string content, err;
    if (!readWholeFile(resolveDataPath(L, 1), content, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushlstring(L, content.data(), content.size());
    return 1;
}

// data.write(archivo, contenido) -> true | nil, err
int l_data_write(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    std::string err;
    if (!writeWholeFile(resolveDataPath(L, 1), std::string(data, len), err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// data.exists(archivo) -> bool
int l_data_exists(lua_State* L) {
    std::error_code ec;
    lua_pushboolean(L, fs::exists(resolveDataPath(L, 1), ec) ? 1 : 0);
    return 1;
}

// data.delete(file) -> bool (false if it did not exist or failed)
int l_data_delete(lua_State* L) {
    std::error_code ec;
    bool ok = fs::remove(resolveDataPath(L, 1), ec);
    lua_pushboolean(L, (ok && !ec) ? 1 : 0);
    return 1;
}

// data.list() -> { nombres... } (archivos y subcarpetas, planos)
int l_data_list(lua_State* L) {
    std::string dir = LuaPlugin::from(L)->dataDir();
    lua_createtable(L, 0, 0);
    int i = 1;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (!ec) {
        for (const auto& e : it) {
            lua_pushstring(L, e.path().filename().string().c_str());
            lua_rawseti(L, -2, i++);
        }
    }
    return 1;
}

// data.loadYml(archivo) -> valor | nil, err
int l_data_loadYml(lua_State* L) {
    std::string content, err;
    if (!readWholeFile(resolveDataPath(L, 1), content, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    if (!yaml::parseToLua(L, content, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    return 1;
}

// data.saveYml(archivo, valor) -> true | nil, err
// (accepts a nested table or a root scalar; non-string keys error out)
int l_data_saveYml(lua_State* L) {
    resolveDataPath(L, 1); // valida la ruta primero
    std::string text, err;
    if (!yaml::fromLua(L, 2, text, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    if (!writeWholeFile(resolveDataPath(L, 1), text, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// data.loadJson(archivo) -> valor | nil, err
int l_data_loadJson(lua_State* L) {
    std::string content, err;
    if (!readWholeFile(resolveDataPath(L, 1), content, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    if (!json::parseToLua(L, content, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    return 1;
}

// data.saveJson(archivo, tabla) -> true | nil, err
int l_data_saveJson(lua_State* L) {
    resolveDataPath(L, 1); // valida la ruta primero
    std::string text, err;
    if (!json::fromLua(L, 2, text, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    if (!writeWholeFile(resolveDataPath(L, 1), text, err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

} // namespace

void bindData(lua_State* L) {
    static const luaL_Reg dataF[] = {{"read", l_data_read},         {"write", l_data_write},
                                     {"exists", l_data_exists},     {"delete", l_data_delete},
                                     {"list", l_data_list},         {"loadYml", l_data_loadYml},
                                     {"saveYml", l_data_saveYml},   {"loadJson", l_data_loadJson},
                                     {"saveJson", l_data_saveJson}, {nullptr, nullptr}};

    lua_newtable(L);
    luaL_setfuncs(L, dataF, 0);
    // Campo fijo: data.folder (ruta de la carpeta de datos de este plugin).
    lua_pushstring(L, LuaPlugin::from(L)->dataDir().c_str());
    lua_setfield(L, -2, "folder");
    lua_setglobal(L, "data");
}

} // namespace cursestone::plugin
