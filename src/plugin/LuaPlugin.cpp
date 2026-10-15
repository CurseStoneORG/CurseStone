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
// LuaPlugin — implementación. Ver LuaPlugin.h para el ciclo de vida.
#include "LuaPlugin.h"
#include "LuaBindings.h"
#include "PathSafe.h"
#include "MiniYaml.h"
#include "event/EventManager.h"
#include "scheduler/ServerScheduler.h"
#include "utils/MainLogger.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

namespace cursestone::plugin {

LuaPlugin::LuaPlugin(std::string dir, std::string dataRoot)
    : dir_(std::move(dir)), dataRoot_(std::move(dataRoot)) {
    folderName_ = std::filesystem::path(dir_).filename().string();
    dataDir_ = dataRoot_ + "/" + folderName_;
    resourcesDir_ = dir_ + "/resources";
}

LuaPlugin::~LuaPlugin() {
    // Si el plugin sigue habilitado al destruirse, desenganchar todo
    // primero (listeners + tareas) para no dejar callbacks colgantes.
    if (enabled_) disable();
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

LuaPlugin* LuaPlugin::from(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "cursestone.plugin");
    LuaPlugin* p = static_cast<LuaPlugin*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return p;
}

// ---------------------------------------------------------------------------
// Carpeta de datos del plugin (data_pl/<carpeta>)
// ---------------------------------------------------------------------------
bool LuaPlugin::ensureDataDir() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dataDir_, ec);
    if (ec) {
        utils::MainLogger::get().error("[" + name_ + "] could not create data folder '" +
                                       dataDir_ + "': " + ec.message());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Manifiesto plugin.yml
// ---------------------------------------------------------------------------
bool LuaPlugin::loadManifest() {
    namespace fs = std::filesystem;

    // ---- plugin.yml es el ÚNICO manifiesto aceptado: un main.lua suelto
    //      (formato antiguo) no carga ya como plugin, y PluginManager
    //      tampoco lo escanea.
    std::error_code ec;
    std::string manifestPath = dir_ + "/plugin.yml";
    if (!fs::exists(manifestPath, ec)) {
        utils::MainLogger::get().error("[" + dir_ +
            "] has no plugin.yml — it is the ONLY accepted manifest "
            "(api/name/author/main)");
        return false;
    }

    // ---- Leer plugin.yml y parsearlo (subconjunto YAML, parser interno
    //      del binario: no se necesita ningún intérprete externo). ----
    std::ifstream in(manifestPath, std::ios::binary);
    if (!in) {
        utils::MainLogger::get().error("[" + dir_ + "] could not open plugin.yml");
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    std::string err;
    if (!yaml::parseToLua(L_, text, err)) {
        utils::MainLogger::get().error("[" + dir_ + "] plugin.yml: " + err);
        return false;
    }
    if (!lua_istable(L_, -1)) {
        utils::MainLogger::get().error("[" + dir_ + "] plugin.yml must be a map of keys");
        lua_pop(L_, 1);
        return false;
    }

    // api (requerido por convención; si falta se asume la del servidor).
    lua_getfield(L_, -1, "api");
    api_ = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : CURSESTONE_API;
    lua_pop(L_, 1);

    // name (si falta se usa el nombre de la carpeta, más abajo).
    lua_getfield(L_, -1, "name");
    if (lua_isstring(L_, -1)) name_ = lua_tostring(L_, -1);
    lua_pop(L_, 1);

    // author (opcional).
    lua_getfield(L_, -1, "author");
    if (lua_isstring(L_, -1)) author_ = lua_tostring(L_, -1);
    lua_pop(L_, 1);

    // version (opcional en el manifiesto; el script de entrada también puede darla).
    lua_getfield(L_, -1, "version");
    if (lua_isstring(L_, -1)) version_ = lua_tostring(L_, -1);
    lua_pop(L_, 1);

    // main: entrada del plugin. SIEMPRE dentro de src/ — la estructura
    // canónica de un plugin CurseStone es:
    //
    //     MiPlugin/
    //     ├── plugin.yml        (manifiesto, raíz del plugin)
    //     └── src/
    //         └── main.lua      (punto de entrada; módulos al lado)
    //
    // plugin.yml y main.lua JUNTOS en la raíz NO se acepta (estructura
    // vieja/fea): se rechaza con un mensaje que dice cómo arreglarlo.
    lua_getfield(L_, -1, "main");
    if (lua_isstring(L_, -1)) {
        std::string rel;
        if (!sanitizeRelPath(lua_tostring(L_, -1), rel)) {
            utils::MainLogger::get().error("[" + dir_ +
                "] plugin.yml: invalid 'main' field (absolute paths and '..' are not allowed)");
            lua_pop(L_, 2);
            return false;
        }
        if (rel.compare(0, 4, "src/") != 0) {
            utils::MainLogger::get().error("[" + dir_ +
                "] plugin.yml: 'main' must live inside src/ (e.g. src/main.lua) — got '" + rel + "'");
            lua_pop(L_, 2);
            return false;
        }
        entry_ = rel;
    }
    lua_pop(L_, 1);
    lua_pop(L_, 1);                     // quitar la tabla del manifiesto

    // ---- Chequeo de API ----
    bool exact = false;
    if (!apiCompatible(api_, &exact)) {
        utils::MainLogger::get().error("[" + (name_.empty() ? dir_ : name_) +
            "] requires API " + api_ + ", but this server provides API " +
            CURSESTONE_API + " — plugin discarded");
        return false;
    }
    if (!exact) {
        utils::MainLogger::get().warn("[" + dir_ + "] declares API " + api_ +
            " while the server provides " + CURSESTONE_API +
            " (compatible, but not exact)");
    }

    // ---- Resolver la entrada si el manifiesto no decía `main`. ----
    // Sin `main:` la entrada es SIEMPRE src/main.lua (no hay fallback a
    // main.lua en la raíz: plugin.yml y main.lua nunca conviven juntos).
    if (entry_.empty()) {
        std::error_code e3;
        std::error_code e5;
        if (fs::exists(dir_ + "/src/main.lua", e3)) {
            entry_ = "src/main.lua";
        } else if (fs::exists(dir_ + "/main.lua", e5)) {
            // Estructura vieja: el main suelto junto a plugin.yml. Error
            // claro y accionable en vez de cargarlo a medias.
            utils::MainLogger::get().error("[" + dir_ +
                "] has main.lua next to plugin.yml — that layout is not "
                "accepted anymore. Create a src/ folder and move the "
                "entry inside: " + dir_ + "/src/main.lua");
            return false;
        } else {
            utils::MainLogger::get().error("[" + dir_ +
                "] plugin.yml has no 'main' and there is no src/main.lua "
                "(the entry script must be at src/main.lua)");
            return false;
        }
    }

    // La entrada debe existir (error claro si no).
    std::error_code e4;
    if (!fs::exists(dir_ + "/" + entry_, e4)) {
        utils::MainLogger::get().error("[" + dir_ + "] entry script '" +
                                       entry_ + "' does not exist");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Carga segura de archivos Lua del propio plugin (plugin.include / import)
// ---------------------------------------------------------------------------

// Núcleo común: rel ya viene validado con sanitizeRelPath. Caché tipo
// require compartida por include() e import() (la clave es la ruta
// resuelta relativa a la carpeta del plugin).
int LuaPlugin::loadModule(lua_State* L, const std::string& rel, const char* what) {
    // Caché tipo require: si ya se cargó, devolver el mismo valor.
    auto it = included_.find(rel);
    if (it != included_.end()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
        return 1;
    }

    std::string full = dir_ + "/" + rel;
    std::error_code ec;
    if (!std::filesystem::exists(full, ec)) {
        return luaL_error(L, "%s: '%s' does not exist", what, rel.c_str());
    }

    // Cargar y ejecutar el chunk (pcall), dejando su valor de retorno.
    // luaL_loadfile ya usa la ruta como chunkname para los errores.
    if (luaL_loadfile(L, full.c_str()) != LUA_OK) {
        const char* msg = lua_tostring(L, -1);
        std::string e = msg ? msg : "could not read file";
        lua_pop(L, 1);
        return luaL_error(L, "%s('%s'): %s", what, rel.c_str(), e.c_str());
    }
    if (!pcall(0, 1)) return luaL_error(L, "%s('%s'): chunk failed", what, rel.c_str());

    // Si devolvió nil, luaL_ref devolvería LUA_NOREF: no cachear en ese caso.
    if (lua_isnil(L, -1)) return 1;

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    included_[rel] = ref;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

// plugin.include(ruta): forma explícita. La ruta es RELATIVA A LA CARPETA
// del plugin y debe ser exacta (con extensión): "src/reglas.lua".
int LuaPlugin::include(lua_State* L) {
    const char* req = luaL_checkstring(L, 1);
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        return luaL_error(L,
            "plugin.include: invalid path '%s' (only relative paths inside the plugin, no '..')", req);
    }
    return loadModule(L, rel, "plugin.include");
}

// import(ruta): la forma cómoda de cargar módulos. Busca primero en src/
// y no exige extensión, y TAMBIÉN acepta nombres de módulo con puntos
// (estilo require), donde cada punto es un nivel de carpeta:
//   import("core/Example")     -> <plugin>/src/core/Example.lua
//   import("core.Example")     -> <plugin>/src/core/Example.lua (puntos = carpetas)
//   import("core/Example.lua") -> <plugin>/src/core/Example.lua
//   import("lib/extra.lua")    -> <plugin>/lib/extra.lua (si no está en src/)
//   import("src/core/Example") -> <plugin>/src/core/Example.lua
// Misma caché que plugin.include: un archivo se ejecuta UNA sola vez.
int LuaPlugin::import(lua_State* L) {
    const char* req = luaL_checkstring(L, 1);
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        return luaL_error(L,
            "import: invalid path '%s' (only relative paths inside the plugin, no '..')", req);
    }

    // Expande un candidato base p a la lista de rutas probadas (igual que
    // siempre: primero en src/, luego en la raíz del plugin).
    auto expand = [](std::vector<std::string>& out, const std::string& p) {
        bool hasExt = p.size() > 4 && p.compare(p.size() - 4, 4, ".lua") == 0;
        bool inSrc  = p.size() >= 4 && p.compare(0, 4, "src/") == 0;
        if (hasExt) {
            if (!inSrc) out.push_back("src/" + p);
            out.push_back(p);
        } else {
            if (!inSrc) out.push_back("src/" + p + ".lua");
            out.push_back(p + ".lua");
            if (!inSrc) out.push_back("src/" + p);
            out.push_back(p);
        }
    };

    std::vector<std::string> cands;
    expand(cands, rel);

    bool hasSlash = rel.find('/') != std::string::npos;
    bool hasExt   = rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".lua") == 0;
    if (!hasSlash && !hasExt && rel.find('.') != std::string::npos) {
        // Estilo require: los puntos son separadores de carpeta
        // ("core.Example" -> "core/example"). Se prueban ANTES que el
        // nombre literal con puntos (así "core.example" carga
        // src/core/example.lua aunque exista un archivo "core.example.lua").
        std::string dotted = rel;
        for (char& c : dotted)
            if (c == '.') c = '/';
        expand(cands, dotted);
    }

    std::error_code ec;
    std::string found, tried;
    for (const auto& c : cands) {
        if (!tried.empty()) tried += ", ";
        tried += "'" + c + "'";
        if (std::filesystem::exists(dir_ + "/" + c, ec)) { found = c; break; }
    }
    if (found.empty()) {
        return luaL_error(L, "import: module '%s' not found (tried %s)",
                          req, tried.c_str());
    }
    return loadModule(L, found, "import");
}

// ---------------------------------------------------------------------------
// load / enable / disable / pcall / tareas
// ---------------------------------------------------------------------------
bool LuaPlugin::load() {
    // 1. State nuevo.
    L_ = luaL_newstate();
    if (!L_) {
        utils::MainLogger::get().error("Could not create the lua_State for " + dir_);
        return false;
    }

    // 2. Solo las libs seguras (NUNCA luaL_openlibs: abriría io/os/package/debug).
    luaL_requiref(L_, "_G", luaopen_base, 1);   lua_pop(L_, 1);
    luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L_, 1);
    luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);  lua_pop(L_, 1);
    luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);  lua_pop(L_, 1);
    luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);  lua_pop(L_, 1);

    // 3. Quitar de _G lo que permite cargar código externo (el acceso a
    //    archivos del plugin pasa EXCLUSIVAMENTE por import() /
    //    plugin.include() y por la API data.*, que sanitizan rutas).
    lua_pushnil(L_); lua_setglobal(L_, "dofile");
    lua_pushnil(L_); lua_setglobal(L_, "loadfile");
    lua_pushnil(L_); lua_setglobal(L_, "load");

    // 4. Guardar `this` en el registry para que from() funcione en los bindings.
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, "cursestone.plugin");

    // 5. Nombre provisional (el de la carpeta) para que los logs del
    //    manifiesto ya tengan sentido; el manifiesto lo puede sobreescribir.
    name_ = folderName_;

    // 6. Manifiesto: api/name/author/version/main + validación de API + entrada.
    if (!loadManifest()) return false;

    // 7. Carpeta de datos data_pl/<carpeta> (fallar el load si no se puede).
    if (!ensureDataDir()) return false;

    // 8. Registrar toda la API (log, server, player, scheduler, plugin,
    //    data, commands, events) DESPUÉS del manifiesto, así la tabla
    //    `plugin` ya expone name/author/version reales.
    registerApi(L_);

    // 9. Ejecutar el script de entrada; su tabla es la tabla del plugin.
    if (luaL_loadfile(L_, (dir_ + "/" + entry_).c_str()) != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        utils::MainLogger::get().error("[" + name_ + "] error: " +
                                       (msg ? msg : "could not read " + entry_));
        lua_pop(L_, 1);
        return false;
    }
    if (!pcall(0, 1)) return false;

    if (!lua_istable(L_, -1)) {
        utils::MainLogger::get().error("[" + name_ + "] " + entry_ +
                                       " must return a table (with onEnable/onDisable)");
        lua_pop(L_, 1);
        return false;
    }

    // El script de entrada puede aportar version (compat con formato antiguo,
    // donde la propia tabla traía name/version). Si la aporta, sincronizar
    // también plugin.version (bindPluginTable ya puso la del manifiesto).
    lua_getfield(L_, -1, "version");
    if (lua_isstring(L_, -1)) {
        version_ = lua_tostring(L_, -1);
        lua_getglobal(L_, "plugin");
        lua_pushstring(L_, version_.c_str());
        lua_setfield(L_, -2, "version");
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    // 10. Guardar la ref de la tabla del plugin.
    tableRef_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    return true;
}

bool LuaPlugin::enable() {
    if (enabled_) return true;
    // enabled_=true ANTES de onEnable para que scheduler/events ya funcionen dentro.
    enabled_ = true;

    bool ok = true;
    if (tableRef_ != LUA_NOREF) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, tableRef_);
        lua_getfield(L_, -1, "onEnable");
        if (lua_isfunction(L_, -1)) {
            lua_remove(L_, -2);          // sacar la tabla, dejar solo la función
            ok = pcall(0, 0);
        } else {
            // La tabla no trae onEnable: aceptar también la función global
            // onEnable definida en el script de entrada.
            lua_pop(L_, 2);
            lua_getglobal(L_, "onEnable");
            if (lua_isfunction(L_, -1)) ok = pcall(0, 0);
            else lua_pop(L_, 1);
        }
    }

    if (!ok) {
        // onEnable falló: deshabilitar (limpia listeners/tareas que quedaran).
        disable();
        return false;
    }

    std::string line = "Plugin enabled: " + name_ + " v" + version_;
    if (!author_.empty()) line += " by " + author_;
    utils::MainLogger::get().info(line);
    return true;
}

void LuaPlugin::disable() {
    if (!enabled_) return;               // ya está deshabilitado: no hacer nada
    enabled_ = false;

    // onDisable (si existe) con pcall. Igual que en enable(): la tabla
    // manda, y si no la trae se acepta la global onDisable.
    if (tableRef_ != LUA_NOREF) {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, tableRef_);
        lua_getfield(L_, -1, "onDisable");
        if (lua_isfunction(L_, -1)) {
            lua_remove(L_, -2);
            pcall(0, 0);
        } else {
            lua_pop(L_, 2);
            lua_getglobal(L_, "onDisable");
            if (lua_isfunction(L_, -1)) pcall(0, 0);
            else lua_pop(L_, 1);
        }
    }

    // Quitar todos los listeners registrados por este plugin (owner = this).
    event::EventManager::get().unlistenAll(this);

    // Cancelar todas las tareas pendientes del scheduler.
    cancelAllTasks();

    utils::MainLogger::get().info("Plugin disabled: " + name_);
}

bool LuaPlugin::pcall(int nargs, int nresults) {
    if (lua_pcall(L_, nargs, nresults, 0) != LUA_OK) {
        const char* msg = lua_tostring(L_, -1);
        utils::MainLogger::get().error("[" + name_ + "] error: " + (msg ? msg : "unknown error"));
        lua_pop(L_, 1);                  // dejar la pila limpia
        return false;
    }
    return true;
}

void LuaPlugin::addTask(uint64_t id, int ref) {
    tasks_[id] = ref;
}

void LuaPlugin::cancelTask(uint64_t id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return;
    scheduler::ServerScheduler::get().cancel(id);
    luaL_unref(L_, LUA_REGISTRYINDEX, it->second);
    tasks_.erase(it);
}

void LuaPlugin::releaseTask(uint64_t id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return;
    luaL_unref(L_, LUA_REGISTRYINDEX, it->second);
    tasks_.erase(it);
}

void LuaPlugin::cancelAllTasks() {
    for (auto& [id, ref] : tasks_) {
        scheduler::ServerScheduler::get().cancel(id);
        luaL_unref(L_, LUA_REGISTRYINDEX, ref);
    }
    tasks_.clear();
}

}
