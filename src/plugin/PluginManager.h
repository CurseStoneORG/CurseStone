/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// PluginManager — escanea <pluginsDir>/<name>/plugin.yml (the only
// manifest accepted: a main.lua loose not load already as plugin),
// load and enables each plugin in alphabetical order and the disables
// all when shutting down the server.
//
// duplicates: if two or more folders declaran the same name of plugin
// (or use the same name of folder), all remain disabled and is
// emits a warning indicando that there is plugins duplicates.
//
// It also ensures the data folders: <dataPath>/data_pl/ (server root, outside
// plugins/) and data_pl/<pluginFolder>/ for every loaded plugin. the
// plugins only can touch its folder through the API data.*.
// Singleton: server::start flame loadAll() and server::runTickLoop flame
// disableAll() tras leaving of the tick loop.
#pragma once
#include "LuaPlugin.h"
#include <memory>
#include <vector>
#include <string>

namespace cursestone::plugin {

class PluginManager {
public:
    static PluginManager& get();

    /**
     * @brief Scans pluginsDir, then loads and enables every plugin.
     *
     * Creates pluginsDir and dataRoot, and moves to legacy
     * <pluginsDir>/data_pl into dataRoot when dataRoot does not exist yet.
     *
     * @param pluginsDir  folder holding one subfolder per plugin.
     * @param dataRoot    root of the per-plugin data folders (data_pl).
     */
    void loadAll(const std::string& pluginsDir, const std::string& dataRoot);

    // /plugins reload: only thread main (touches Lua). disables all
    // (onDisable + listeners + tareas), removes the commands of the plugins
    // of the CommandMap, destroys the lua_States and scans again to scan and load
    // pluginsDir_ from zero. returns how many plugins remain loaded.
    size_t reloadAll();

    // disables all (is flame when shutting down the server).
    void disableAll();

    struct Info { std::string name; std::string author; std::string version; bool enabled; };
    std::vector<Info> list() const;

    /// @brief returns the plugin with that name, or nullptr if not exists.
    ///        Resolución in CALIENTE: prevents retain pointers of LuaPlugin
    ///        more allá of the tick in that is obtuvieron (a /plugins reload
    ///        destroys and recrea the objetos).
    LuaPlugin* find(const std::string& name) const;

private:
    PluginManager() = default;
    std::string pluginsDir_;            // stored by loadAll() for reloadAll()
    std::string dataRoot_;              // data_pl root, remembered for reloadAll()
    std::vector<std::unique_ptr<LuaPlugin>> plugins_;

    // without onDisable (already it hizo disableAll()): quita the commands of the
    // plugins of the CommandMap and destroys the lua_States.
    void unloadAll();
};

}
