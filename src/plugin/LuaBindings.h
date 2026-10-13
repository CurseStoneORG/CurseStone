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
// LuaBindings — punto de registro de toda la API Lua expuesta a los
// plugins. Cada bloque agrega su propia función bind*:
//   - bindCore      : log, server, player, scheduler + tabla `plugin`
//   - bindData      : data.read/write/exists/delete/list + loadYml/saveYml/
//                     loadJson/saveJson (restringido a data_pl/<plugin>)
//   - bindCommands  : commands.register / commands.reply
//   - bindEvents    : events.on
// registerApi() las llama todas en orden.
#pragma once
#include <lua.hpp>
#include "CurseStone.h"   // CURSESTONE_API_VERSION (única fuente de verdad)
#include <string>

namespace cursestone::plugin {

// ---- Versión de la API de plugins del servidor ----
// Espejo de CURSESTONE_API_VERSION (CurseStone.h): un solo sitio para
// cambiarla. Los plugins declaran la API con la que fueron escritos en su
// plugin.yml (campo `api`). Regla de compatibilidad (ver apiCompatible):
// mismo major obligatorio; minor del servidor >= minor del plugin.
inline constexpr const char* PLUGIN_API_VERSION = CURSESTONE_API_VERSION;

// Compara "X.Y.Z" de un plugin contra la API del servidor.
//   - true  : compatible (se puede cargar; puede emitir warn si no es exacta)
//   - false : incompatible (el plugin se descarta)
// Devuelve en `exact` si la versión coincide exactamente (para el warn).
// Implementación en LuaBindings.cpp.
bool apiCompatible(const std::string& pluginApi, bool* exact);

// Registra toda la API en el state. Los binds agregan sus llamadas aquí.
void registerApi(lua_State* L);

// API core: log, server, player, scheduler y la tabla `plugin`.
void bindCore(lua_State* L);

// API de datos/configs (carpeta data_pl/<plugin>).
void bindData(lua_State* L);

// API de comandos.
void bindCommands(lua_State* L);

// API de eventos.
void bindEvents(lua_State* L);

} // namespace cursestone::plugin
