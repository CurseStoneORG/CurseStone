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
// LuaBindings — implementación del registro de la API.
// registerApi() se llama desde LuaPlugin::load() justo después de abrir
// las librerías estándar permitidas y ANTES de ejecutar main.lua, de
// modo que el script ya encuentra todo el entorno montado.
#include "LuaBindings.h"
#include <cstdio>
#include <cstdlib>

namespace cursestone::plugin {

// ---- Compatibilidad de versiones de API ("X.Y.Z") ----
// Reglas:
//   - Formato inválido (no son 3 números)            -> incompatible.
//   - Major distinto                                  -> incompatible (rompe).
//   - Minor del servidor < minor del plugin           -> incompatible
//     (el plugin necesita una API más nueva que esta).
//   - Resto (minor mayor o igual, patch cualquiera)   -> compatible.
// `exact` queda true solo si coincide exactamente (para avisar con warn).
static bool parseSemver(const std::string& s, int out[3]) {
    int vals[3] = {0, 0, 0};
    int idx = 0, n = 0;
    for (char c : s) {
        if (c == '.') {
            if (n == 0) return false;      // ".x" o ".."
            if (++idx > 2) return false;   // más de 3 componentes
            n = 0;
            continue;
        }
        if (c < '0' || c > '9') return false;
        vals[idx] = vals[idx] * 10 + (c - '0');
        ++n;
    }
    if (n == 0) return false;              // termina en "." o vacío
    out[0] = vals[0]; out[1] = vals[1]; out[2] = vals[2];
    return true;
}

bool apiCompatible(const std::string& pluginApi, bool* exact) {
    if (exact) *exact = false;
    int pl[3] = {0, 0, 0}, sv[3] = {0, 0, 0};
    if (!parseSemver(pluginApi, pl)) return false;
    if (!parseSemver(PLUGIN_API_VERSION, sv)) return false;
    if (pl[0] != sv[0]) return false;      // major distinto: rompe
    if (pl[1] > sv[1])  return false;      // plugin pide minor más nueva
    if (exact) *exact = (pl[0] == sv[0] && pl[1] == sv[1] && pl[2] == sv[2]);
    return true;
}

void registerApi(lua_State* L) {
    bindCore(L);      // log, server, player, scheduler + tabla plugin
    bindData(L);      // data.read/write/exists/delete/list + loadYml/saveYml/loadJson/saveJson
    bindCommands(L);  // commands.register / commands.reply
    bindEvents(L);    // events.on
}

} // namespace cursestone::plugin
