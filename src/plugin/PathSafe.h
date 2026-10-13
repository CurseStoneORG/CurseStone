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
// PathSafe — normalización de rutas relativas usada por TODO el sistema
// de plugins (plugin.include(), data.*). Viva en un header inline para que
// LuaPlugin y los bindings de datos compartan EXACTAMENTE las mismas
// reglas de seguridad:
//
//   - Rechaza cadenas vacías.
//   - Rechaza rutas absolutas (empiezan con '/').
//   - Rechaza backslashes (separador no portable; en Windows confundiría
//     el chequeo de ".." — se prohíbe de raíz).
//   - Rechaza cualquier componente "..": NUNCA se puede salir de la
//     carpeta base (data_pl/<plugin> o la carpeta del plugin).
//   - Normaliza "./src//x.lua" -> "src/x.lua" (colapsa '/' y '.').
#pragma once
#include <string>

namespace cursestone::plugin {

inline bool sanitizeRelPath(const std::string& in, std::string& out) {
    if (in.empty() || in[0] == '/' || in.find('\\') != std::string::npos) return false;
    std::string clean;
    size_t i = 0;
    while (i <= in.size()) {
        size_t j = in.find('/', i);
        if (j == std::string::npos) j = in.size();
        std::string part = in.substr(i, j - i);
        if (!part.empty() && part != ".") {
            if (part == "..") return false;         // NO salirse de la carpeta base
            if (!clean.empty()) clean += '/';
            clean += part;
        }
        i = j + 1;
    }
    if (clean.empty()) return false;
    out = clean;
    return true;
}

} // namespace cursestone::plugin
