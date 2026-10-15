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
// LuaBindings — point of registro of all the API Lua expuesta to the
// plugins. each block adds its own function bind*:
//   - bindCore      : log, server, player, scheduler + tabla `plugin`
//   - bindData      : data.read/write/exists/delete/list + loadYml/saveYml/
//                     loadJson/saveJson (restringido to data_pl/<plugin>)
//   - bindCommands  : commands.register / commands.reply
//   - bindEvents    : events.on
// registerApi() the flame all in order.
#pragma once
#include <lua.hpp>
#include "CurseStone.h" // CURSESTONE_API — the ONLY version constant (unified:
                        // server version == plugin API version)
#include <string>

namespace cursestone::plugin {

// ---- version of the API of plugins of the server ----
// UNIFIED: there is no mirror here anymore. The single constant is the
// macro CURSESTONE_API in CurseStone.h (server version == plugin API
// version). Plugins declare it in their plugin.yml (field `api`).
// Regla of compatibilidad (ver apiCompatible):
// same major required; minor of the server >= minor of the plugin.

// compares "X.and.Z" of a plugin contra the API of the server.
//   - true  : compatible (is can load; can emit warn if not is exact)
//   - false : incompatible (the plugin is descarta)
// returns in `exact` if the version matches exactly (for the warn).
// Implementación in LuaBindings.cpp.
bool apiCompatible(const std::string& pluginApi, bool* exact);

// registers all the API in the state. the binds add its calls here.
void registerApi(lua_State* L);

// API core: log, server, player, scheduler and the tabla `plugin`.
void bindCore(lua_State* L);

// API of data/configs (folder data_pl/<plugin>).
void bindData(lua_State* L);

// API of commands.
void bindCommands(lua_State* L);

// API of eventos.
void bindEvents(lua_State* L);

// API of worlds: server.worlds/getWorld/loadWorld/defaultWorld and the
// methods of the object world (getBlock, setBlock, getPlayers, ...).
void bindWorld(lua_State* L);

// pushes in the stack a handle of world for Lua (userdata with the
// name; nil if w == nullptr). the use bindWorld and bindCore for
// player.getWorld.
void pushWorldHandle(lua_State* L, World* w);

} // namespace cursestone::plugin
