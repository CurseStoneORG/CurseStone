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
// PluginManager — implementación.
#include "PluginManager.h"
#include "LuaBindings.h"              // PLUGIN_API_VERSION
#include "MiniYaml.h"                 // leer el campo `name` de plugin.yml
#include "Server.h"                   // getCommandMap() para /plugins reload
#include "command/CommandMap.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace cursestone::plugin {

namespace {

utils::MainLogger& log() { return utils::MainLogger::get(); }

std::string toLowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Lee SOLO el campo `name` del plugin.yml de una carpeta usando un
// lua_State temporal: parsea el YAML (parser interno del binario) pero NO
// ejecuta nada del plugin. Devuelve "" si no se pudo leer.
std::string manifestName(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir + "/plugin.yml", ec)) return "";
    std::ifstream in(dir + "/plugin.yml", std::ios::binary);
    if (!in) return "";
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    lua_State* L = luaL_newstate();
    if (!L) return "";
    std::string out, err;
    if (yaml::parseToLua(L, text, err) && lua_istable(L, -1)) {
        lua_getfield(L, -1, "name");
        if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, lua_gettop(L));          // pila limpia antes de cerrar el state temporal
    lua_close(L);
    return out;
}

} // namespace

PluginManager& PluginManager::get() {
    static PluginManager inst;
    return inst;
}

void PluginManager::loadAll(const std::string& pluginsDir,
                            const std::string& dataRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;

    pluginsDir_ = pluginsDir;           // para /plugins reload
    dataRoot_ = dataRoot;
    log().info("Plugin API: " + std::string(PLUGIN_API_VERSION));

    // Crear la carpeta plugins/ si no existe (primera ejecución).
    fs::create_directories(pluginsDir, ec);
    ec.clear();

    // Data root lives outside plugins/. Move the legacy location once.
    const std::string legacyRoot = pluginsDir + "/data_pl";
    if (fs::exists(legacyRoot, ec) && !fs::exists(dataRoot, ec)) {
        ec.clear();
        fs::rename(legacyRoot, dataRoot, ec);
        if (ec) {
            log().warn("[Plugins] Could not move " + legacyRoot + " to " +
                       dataRoot + ": " + ec.message());
        } else {
            log().info("[Plugins] Moved " + legacyRoot + " to " + dataRoot);
        }
    }
    ec.clear();
    fs::create_directories(dataRoot, ec);
    ec.clear();

    // ---- 1. Escaneo. SOLO se acepta plugin.yml como manifiesto: un
    //         main.lua suelto (formato antiguo) no carga ya como plugin.
    //         Por eso, si dos carpetas traen "el mismo" plugin, el válido
    //         es directamente el que tiene plugin.yml — las demás ni
    //         entran a la lista.
    struct Cand { std::string dir; std::string folder; std::string name; };
    std::vector<Cand> cands;

    fs::directory_iterator it(pluginsDir, ec);
    if (!ec) {
        for (const auto& entry : it) {
            std::error_code e2;
            if (!entry.is_directory(e2)) continue;
            if (entry.path().filename() == "data_pl") continue;   // datos, no plugin
            std::string dir = entry.path().string();
            std::string folder = entry.path().filename().string();

            if (fs::exists(entry.path() / "plugin.yml", e2)) {
                cands.push_back(Cand{dir, folder, manifestName(dir)});
                continue;
            }
            // Sin plugin.yml NO es un plugin. Warn only when it LOOKS like
            // a plugin attempt (a loose main.lua) to keep the log clean of
            // random user folders.
            std::error_code e3;
            if (fs::exists(entry.path() / "main.lua", e3)) {
                log().warn("[Plugins] '" + folder +
                    "' is ignored: main.lua without plugin.yml is no longer "
                    "accepted; create a plugin.yml with api/name/author/main.");
            }
            // Any other folder without plugin.yml is silently ignored.
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.dir < b.dir; });

    // ---- 2. Duplicados. Se marca para APAGAR (no cargar) todos los
    //         implicados, con warning claro. Dos claves:
    //         a) mismo nombre de plugin en plugin.yml (insensible a
    //            mayúsculas/minúsculas),
    //         b) mismo nombre de carpeta (también insensible), porque
    //            chocarían sus carpetas de datos en data_pl/.
    std::map<std::string, std::vector<size_t>> byName;
    std::map<std::string, std::vector<size_t>> byFolder;
    for (size_t i = 0; i < cands.size(); ++i) {
        std::string shown = !cands[i].name.empty() ? cands[i].name : cands[i].folder;
        byName[toLowerAscii(shown)].push_back(i);
        byFolder[toLowerAscii(cands[i].folder)].push_back(i);
    }
    std::vector<bool> killed(cands.size(), false);
    for (const auto& [key, idxs] : byName) {
        if (idxs.size() < 2) continue;
        std::string shown = !cands[idxs[0]].name.empty() ? cands[idxs[0]].name
                                                         : cands[idxs[0]].folder;
        std::string paths;
        for (size_t k = 0; k < idxs.size(); ++k) {
            paths += cands[idxs[k]].folder;
            if (k + 1 < idxs.size()) paths += ", ";
        }
        log().warn("[Plugins] WARNING: there are " + std::to_string(idxs.size()) +
                   " duplicate plugins named '" + shown + "' (" + paths +
                   "). All of them are disabled: keep only one copy.");
        for (size_t i : idxs) killed[i] = true;
    }
    for (const auto& [key, idxs] : byFolder) {
        if (idxs.size() < 2) continue;
        bool anyAlive = false;
        for (size_t i : idxs) if (!killed[i]) anyAlive = true;
        if (!anyAlive) continue;        // ya avisado por el nombre de plugin
        std::string paths;
        for (size_t k = 0; k < idxs.size(); ++k) {
            paths += cands[idxs[k]].folder;
            if (k + 1 < idxs.size()) paths += ", ";
        }
        log().warn("[Plugins] WARNING: there are " + std::to_string(idxs.size()) +
                   " duplicate folders named '" + key + "' (" + paths +
                   "). All of them are disabled to avoid data conflicts.");
        for (size_t i : idxs) killed[i] = true;
    }

    // ---- 3. Cargar + habilitar los que quedan. Los que fallen se
    //         descartan (el log ya dijo por qué).
    for (size_t i = 0; i < cands.size(); ++i) {
        if (killed[i]) continue;
        auto p = std::make_unique<LuaPlugin>(cands[i].dir, dataRoot);
        if (p->load() && p->enable()) {
            plugins_.push_back(std::move(p));
        } else {
            log().warn("Plugin discarded: " + cands[i].dir);
        }
    }
    log().info("Plugins loaded: " + std::to_string(plugins_.size()));
}

size_t PluginManager::reloadAll() {
    // SOLO hilo principal (toca lua_States): /plugins reload llega aquí a
    // través de ServerScheduler::scheduleSync desde PluginsCommand.
    if (pluginsDir_.empty()) {
        log().error("[Plugins] Reload failed: no plugins folder has been scanned yet.");
        return plugins_.size();
    }
    log().info("[Plugins] Reloading all plugins...");
    disableAll();                       // onDisable + unlisten + cancelar tareas
    unloadAll();                        // comandos fuera del mapa + lua_close
    loadAll(pluginsDir_, dataRoot_);    // re-escaneo + carga fresca
    return plugins_.size();
}

void PluginManager::unloadAll() {
    // Los LuaCommand guardan refs al registry del lua_State que estamos a
    // punto de cerrar: salirlos del CommandMap PRIMERO, o un /<cmd> tras
    // el reload tocaría un state muerto.
    if (CommandMap* map = Server::getInstance().getCommandMap()) {
        for (auto& p : plugins_) map->unregisterByOwner(p.get());
    }
    // Destruir cada plugin (~LuaPlugin cierra el state). El disable ya se
    // hizo (reloadAll -> disableAll); si alguien llama unloadAll a pelo,
    // ~LuaPlugin igualmente deshabilita los que sigan activos.
    plugins_.clear();
}

void PluginManager::disableAll() {
    for (auto& p : plugins_) p->disable();
}

std::vector<PluginManager::Info> PluginManager::list() const {
    std::vector<Info> out;
    out.reserve(plugins_.size());
    for (const auto& p : plugins_) {
        out.push_back(Info{p->name(), p->author(), p->version(), p->isEnabled()});
    }
    return out;
}

}
