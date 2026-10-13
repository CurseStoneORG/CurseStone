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
// MiniYaml — parser y serializador YAML (subconjunto) sobre lua_State.
//
// Se usa para DOS cosas:
//   1. Leer el manifiesto plugin.yml de cada plugin.
//   2. La API data.loadYml / data.saveYml (configs de los plugins).
//
// El resultado se apila DIRECTAMENTE como valores Lua (tablas anidadas,
// strings, números, booleanos, nil): sin tipos intermedios.
//
// SUBCONJUNTO SOPORTADO (documentado en PLUGINS.md):
//
//   # comentario
//   api: "1.0.0"            # string con comillas
//   name: Hello             # string pelado
//   activado: true          # booleano
//   max: 10                 # entero
//   ratio: 0.5              # flotante
//   vacio: null             # null / ~ -> nil (la clave se omite en Lua)
//   lista:                  # lista de escalares
//     - uno
//     - "dos: con dos puntos"
//   mapa:                   # mapa anidado por indentación (2+ espacios)
//     sub: valor
//   items:                  # lista de mapas:
//     - nombre: a           #   primera clave pegada al "-"
//       extra: 1            #   claves siguientes alineadas
//
//   {} -> tabla vacía, [] -> lista vacía
//
// NO soportado: anchors (&/*), multi-línea (| >), flow anidado completo,
// tabs de indentación (error claro).
#pragma once
#include <lua.hpp>
#include <string>

namespace cursestone::plugin::yaml {

// Parsea `text` y apila UN valor (normalmente una tabla) en la pila de L.
// Devuelve false y llena `err` si hay error de sintaxis.
bool parseToLua(lua_State* L, const std::string& text, std::string& err);

// Serializa la tabla en `idx` a texto YAML. Acepta también escalares en
// la raíz (documento de un solo valor). Devuelve false y llena `err` si
// el valor no es serializable (tabla mixta rara, funciones, etc.).
bool fromLua(lua_State* L, int idx, std::string& out, std::string& err);

} // namespace cursestone::plugin::yaml
