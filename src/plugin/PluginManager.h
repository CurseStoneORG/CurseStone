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
// PluginManager — escanea <pluginsDir>/<nombre>/plugin.yml (el ÚNICO
// manifiesto aceptado: un main.lua suelto no carga ya como plugin),
// carga y habilita cada plugin en orden alfabético y los deshabilita
// todos al apagar el servidor.
//
// Duplicados: si dos o más carpetas declaran el mismo nombre de plugin
// (o usan el mismo nombre de carpeta), TODAS quedan desactivadas y se
// suelta un warning indicando que hay plugins duplicados.
//
// It also ensures the data folders: <dataPath>/data_pl/ (server root, outside
// plugins/) and data_pl/<pluginFolder>/ for every loaded plugin. Los
// plugins SOLO pueden tocar su carpeta a través de la API data.*.
// Singleton: Server::start llama loadAll() y Server::runTickLoop llama
// disableAll() tras salir del tick loop.
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
     * Creates pluginsDir and dataRoot, and moves a legacy
     * <pluginsDir>/data_pl into dataRoot when dataRoot does not exist yet.
     *
     * @param pluginsDir  folder holding one subfolder per plugin.
     * @param dataRoot    root of the per-plugin data folders (data_pl).
     */
    void loadAll(const std::string& pluginsDir, const std::string& dataRoot);

    // /plugins reload: SOLO hilo principal (toca Lua). Deshabilita todos
    // (onDisable + listeners + tareas), borra los comandos de los plugins
    // del CommandMap, destruye los lua_States y vuelve a escanear y cargar
    // pluginsDir_ desde cero. Devuelve cuántos plugins quedaron cargados.
    size_t reloadAll();

    // Deshabilita todos (se llama al apagar el servidor).
    void disableAll();

    struct Info { std::string name; std::string author; std::string version; bool enabled; };
    std::vector<Info> list() const;

private:
    PluginManager() = default;
    std::string pluginsDir_;            // recordado por loadAll() para reloadAll()
    std::string dataRoot_;              // data_pl root, remembered for reloadAll()
    std::vector<std::unique_ptr<LuaPlugin>> plugins_;

    // Sin onDisable (ya lo hizo disableAll()): quita los comandos de los
    // plugins del CommandMap y destruye los lua_States.
    void unloadAll();
};

}
