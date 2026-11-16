/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// PluginManager — the two discovery paths of the plugin system:
//
//   1. FOLDER plugins (Lua): plugins/<Name>/plugin.yml with an entry
//      script under src/ — loaded by the LuaPlugin backend.
//   2. FILE plugins (native): a bare plugins/<Name>.so or .dll dropped
//      directly into plugins/ — no folder, no plugin.yml; the vtable
//      descriptor inside the library is the manifest (NativePlugin).
//
// Both kinds load, enable and disable in one alphabetical pass and are
// disabled again on server shutdown.
//
// duplicates: two folders declaring the same plugin name, two folders
// with the same name, two native files with the same stem, or any
// folder/stem collision in the shared data_pl namespace all disable the
// copies involved with a warning. A native descriptor name that collides
// with an already-loaded plugin is rejected after load.
//
// It also ensures the data folders: <dataPath>/data_pl/ (server root, outside
// plugins/) and data_pl/<folder or stem>/ for every loaded plugin. the
// plugins only can touch its folder through the API data.*.
// Singleton: server::start flame loadAll() and server::runTickLoop flame
// disableAll() tras leaving of the tick loop.
#pragma once
#include "PluginBase.h"
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

    struct Info {
        std::string name;
        std::string author;
        std::string version;
        std::string kind; // "lua" or "native".
        bool enabled;
    };
    std::vector<Info> list() const;

    /// @brief returns the plugin with that name, or nullptr if not exists.
    ///        Live resolution: never retain the returned PluginBase pointer
    ///        beyond the tick it was obtained in (a /plugins reload
    ///        destroys and recreates the objects). Callers that need the
    ///        concrete backend downcast AFTER the null check.
    PluginBase* find(const std::string& name) const;

  private:
    PluginManager() = default;
    std::string pluginsDir_; // stored by loadAll() for reloadAll()
    std::string dataRoot_;   // data_pl root, remembered for reloadAll()
    std::vector<std::unique_ptr<PluginBase>> plugins_;

    // without onDisable (already it hizo disableAll()): quita the commands of the
    // plugins of the CommandMap and destroys the lua_States.
    void unloadAll();
};

}
