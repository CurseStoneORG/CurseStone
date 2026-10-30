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
// MiniJson — parser y serializador JSON sobre lua_State.
//
// Se usa para la API data.loadJson / data.saveJson (configs y datos
// estructurados de los plugins en data_pl/<plugin>/*.json).
//
// Igual que MiniYaml, el resultado se apila DIRECTAMENTE como valores Lua:
//   objeto  -> tabla {clave=valor}
//   array   -> tabla {1=n, 2=m, ...}
//   string  -> string
//   number  -> integer si no tiene parte fraccional, number si no
//   true/false/null -> booleano / nil
//
// Supports the FULL standard: \uXXXX escapes with surrogate pairs -> UTF-8.
#pragma once
#include <lua.hpp>
#include <string>

namespace cursestone::plugin::json {

// Parsea `text` y apila UN valor. Devuelve false y llena `err` si el JSON
// is invalid (with an approximate offset).
bool parseToLua(lua_State* L, const std::string& text, std::string& err);

// Serializa el valor en `idx` a JSON. Las tablas con parte de array
// (1..n consecutivo) salen como arrays; el resto como objetos.
// Devuelve false y llena `err` ante valores no serializables.
bool fromLua(lua_State* L, int idx, std::string& out, std::string& err);

} // namespace cursestone::plugin::json
