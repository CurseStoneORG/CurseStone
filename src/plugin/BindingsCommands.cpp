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
// BindingsCommands — Fase 5: API de comandos para los plugins Lua.
//
//   commands.register(name, opts, fn)
//       opts (tabla opcional):
//         description  texto que aparece en /help
//         usage        uso ("/cmd <algo>")
//         override     true -> PISA un comando existente (ej. /kick del
//                      nucleo): el original se CANCELA al instante (deja
//                      de ejecutarse para siempre mientras el plugin esté
//                      habilitado) y la funcion del plugin lo reemplaza.
//                      Cuando el plugin se descarga / /plugins reload, el
//                      comando original del nucleo se RESTAURA solo.
//         permission   QUIÉN puede ejecutarlo:
//                        "default"/"all"/"public"/ausente -> todos
//                        "op"/"admin"                     -> solo ops y consola
//                        cualquier otro string            -> nodo custom:
//                          lo tienen ops, consola, y jugadores a los que
//                          otro plugin les concedió el nodo con
//                          player.setPermission(nombre, nodo, true)
//       devuelve true si se registró, false si el nombre ya estaba tomado.
//   commands.reply(sender, msg)
//       responde a quien ejecutó el comando (consola -> log, jugador ->
//       sendMessage). `sender` es la tabla simple que entregó el propio
//       comando: { name, isConsole, isOp }.
//
// Los comandos registrados aparecen en /help (con su description) si el
// emisor tiene permiso para usarlos.
//
// Nota: SimpleCommandMap busca el comando por nombre EXACTO, sin
// normalizar — por eso aquí pasamos el nombre a minúsculas antes de
// registrarlo.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "LuaCommand.h"
#include "Server.h"
#include "command/SimpleCommandMap.h"   // definición completa de CommandMap
#include "Player.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace cursestone::plugin {

namespace {

// commands.register(name, opts, fn) -> boolean
int l_commands_register(lua_State* L) {
    // 1. Nombre (obligatorio) a minúsculas.
    std::string name = luaL_checkstring(L, 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    // 2. opts: tabla opcional. Leer description / usage / permission / override.
    std::string description, usage;
    LuaCommand::PermMode mode = LuaCommand::PermMode::PUBLIC;
    std::string node;
    bool overrideCmd = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "description");
        if (const char* s = lua_tostring(L, -1)) description = s;
        lua_pop(L, 1);
        lua_getfield(L, 2, "usage");
        if (const char* s = lua_tostring(L, -1)) usage = s;
        lua_pop(L, 1);
        lua_getfield(L, 2, "override");
        overrideCmd = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "permission");
        if (const char* s = lua_tostring(L, -1)) {
            std::string p = s;
            std::transform(p.begin(), p.end(), p.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (p == "op" || p == "admin") {
                mode = LuaCommand::PermMode::OP;
            } else if (p != "default" && p != "all" && p != "public" &&
                       p != "everyone" && p != "todos") {
                mode = LuaCommand::PermMode::NODE;
                node = s;                 // el nodo se respeta tal cual (sin bajar)
            }
            // default/all/public/everyone/todos -> PUBLIC (ya está por defecto)
        }
        lua_pop(L, 1);
    }

    // 3. fn (arg 3) debe ser función -> guardar como ref en el registry.
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    int fnRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // 4. Registrar el comando en el CommandMap del servidor. Con
    //    override=true el comando PISA al existente (el original queda de
    //    fallback y vuelve solo cuando este plugin se descargue).
    LuaPlugin* pl = LuaPlugin::from(L);
    auto cmd = std::make_unique<LuaCommand>(pl, fnRef, name, description, usage, mode, node);
    bool ok = false;
    if (overrideCmd) {
        auto* map = Server::getInstance().getCommandMap();
        Command* displaced = map->getCommand(name);
        ok = map->overrideCommand(std::move(cmd));
        if (ok && displaced) {
            utils::MainLogger::get().info(
                "[" + pl->name() + "] overrode command /" + name +
                " (original cancelled, restored on plugin unload)");
        }
    } else {
        ok = Server::getInstance().getCommandMap()->registerCommand(std::move(cmd));
    }

    // 5. Si el nombre ya estaba tomado: soltar la ref y avisar.
    if (!ok) {
        luaL_unref(L, LUA_REGISTRYINDEX, fnRef);
        utils::MainLogger::get().warn(
            "[" + pl->name() + "] command '" + name + "' already exists");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// commands.reply(sender, msg)
int l_commands_reply(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* msg = luaL_checkstring(L, 2);

    lua_getfield(L, 1, "isConsole");
    bool isConsole = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    if (isConsole) {
        // Consola: el "reply" va al log.
        utils::MainLogger::get().info(msg);
        return 0;
    }

    lua_getfield(L, 1, "name");
    const char* name = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (name) {
        Player* p = Server::getInstance().getPlayerExact(name);
        if (p) p->sendMessage(msg);
    }
    return 0;
}

} // namespace

void bindCommands(lua_State* L) {
    static const luaL_Reg cmdsF[] = {
        {"register", l_commands_register}, {"reply", l_commands_reply},
        {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, cmdsF, 0);
    lua_setglobal(L, "commands");
}

} // namespace cursestone::plugin
