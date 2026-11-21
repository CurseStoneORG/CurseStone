/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
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
// BindingsCommands — Fase 5: API de comandos para los plugins Lua.
//
//   commands.register(name, opts, fn)
//       opts (tabla opcional):
//         description  texto que aparece en /help
//         usage        uso ("/cmd <algo>")
//         override     true -> PISA un comando existente (ej. /kick del
//                      nucleo): el original se CANCELA al instante (deja
//                      running forever while the plugin stays
//                      habilitado) y la funcion del plugin lo reemplaza.
//                      Cuando el plugin se descarga / /plugins reload, el
//                      comando original del nucleo se RESTAURA solo.
//         permission   WHO can run it:
//                        "default"/"all"/"public"/ausente -> todos
//                        "op"/"admin"                     -> solo ops y consola
//                        cualquier otro string            -> nodo custom:
//                          lo tienen ops, consola, y jugadores a los que
//                          another plugin granted them the node via
//                          player.setPermission(nombre, nodo, true)
//       returns true when registered, false if the name was taken.
//   commands.registerAlias(alias, target)
//       atajo: /alias despacha /target (target puede llevar argumentos
//       prefijados, los que escriba el emisor se agregan al final). El
//       alias muere con el plugin y el permiso del comando destino manda.
//       false si el nombre estaba tomado o era invalido.
//   commands.unregisterAlias(alias)
//       borra UN alias registrado por ESTE plugin (limpieza en onDisable
//       o re-hacer un atajo). Los alias nativos del software y los de
//       otros plugins no se pueden soltar por esta via. true si existia.
//   commands.reply(sender, msg)
//       answers whoever ran the command (console -> log, player ->
//       sendMessage). `sender` is the plain table handed out by the
//       comando: { name, isConsole, isOp }.
//
// Los comandos registrados aparecen en /help (con su description) si el
// emisor tiene permiso para usarlos.
//
// Nota: SimpleCommandMap busca el comando por nombre EXACTO, sin
// normalize — that is why the name is lowercased here before
// registrarlo.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "LuaCommand.h"
#include "Server.h"
#include "command/SimpleCommandMap.h" // full CommandMap definition
#include "Player.h"
#include "utils/MainLogger.hpp"
#include "utils/Utils.hpp"
#include <string>

namespace cursestone::plugin {

namespace {

// commands.register(name, opts, fn) -> boolean
int l_commands_register(lua_State* L) {
    // 1. Name (required), lowercased.
    std::string name = utils::toLower(luaL_checkstring(L, 1));

    // 2. opts: tabla opcional. Leer description / usage / permission / override.
    std::string description, usage;
    LuaCommand::PermMode mode = LuaCommand::PermMode::PUBLIC;
    std::string node;
    bool overrideCmd = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "description");
        if (const char* s = lua_tostring(L, -1))
            description = s;
        lua_pop(L, 1);
        lua_getfield(L, 2, "usage");
        if (const char* s = lua_tostring(L, -1))
            usage = s;
        lua_pop(L, 1);
        lua_getfield(L, 2, "override");
        overrideCmd = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "permission");
        if (const char* s = lua_tostring(L, -1)) {
            std::string p = utils::toLower(s);
            if (p == "op" || p == "admin") {
                mode = LuaCommand::PermMode::OP;
            } else if (p != "default" && p != "all" && p != "public" && p != "everyone" &&
                       p != "todos") {
                mode = LuaCommand::PermMode::NODE;
                node = s; // el nodo se respeta tal cual (sin bajar)
            }
            // default/all/public/everyone -> PUBLIC (already the default)
        }
        lua_pop(L, 1);
    }

    // 3. fn (arg 3) must be a function -> store as a ref in the registry.
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
            utils::MainLogger::get().info("[" + pl->name() + "] overrode command /" + name +
                                          " (original cancelled, restored on plugin unload)");
        }
    } else {
        ok = Server::getInstance().getCommandMap()->registerCommand(std::move(cmd));
    }

    // 5. Si el nombre ya estaba tomado: soltar la ref y avisar.
    if (!ok) {
        luaL_unref(L, LUA_REGISTRYINDEX, fnRef);
        utils::MainLogger::get().warn("[" + pl->name() + "] command '" + name + "' already exists");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

// commands.registerAlias(alias, target) -> boolean
//
// Registers a shortcut: typing /alias dispatches /target. target may be
// "command" or "command with fixed args" — the arguments the sender types
// are appended after the fixed ones. Real commands always win (a name
// taken by a command or another alias is rejected), the alias dies with
// the plugin (/plugins reload or unload drops it), and the TARGET
// command's permission still gates the call.
//
//   commands.registerAlias("spawn", "tp world spawn")
//   commands.registerAlias("gmc", "gamemode 1")
int l_commands_register_alias(lua_State* L) {
    const char* alias = luaL_checkstring(L, 1);
    const char* target = luaL_checkstring(L, 2);
    LuaPlugin* pl = LuaPlugin::from(L);

    bool ok = Server::getInstance().getCommandMap()->registerAlias(alias, target, pl);
    if (!ok) {
        utils::MainLogger::get().warn("[" + pl->name() + "] alias '/" + std::string(alias) +
                                      "' rejected (name taken, empty, or self-targeted)");
    } else {
        // Honest heads-up, not a failure: the alias may point at a command
        // a LATER plugin registers, but a typo stays silent otherwise.
        const std::string lowered = utils::toLower(target);
        const std::string tName = lowered.substr(0, lowered.find(' '));
        if (!Server::getInstance().getCommandMap()->getCommand(tName))
            utils::MainLogger::get().warn("[" + pl->name() + "] alias '/" + std::string(alias) +
                                          "' -> '/" + tName + "' (target not registered yet)");
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// commands.unregisterAlias(alias) -> boolean
//
// Removes an alias THIS plugin registered earlier (onDisable-time cleanup,
// or re-pointing a shortcut). The software's native aliases and other
// plugins' aliases cannot be dropped through it; false means the name was
// not an owned alias.
int l_commands_unregister_alias(lua_State* L) {
    const char* alias = luaL_checkstring(L, 1);
    LuaPlugin* pl = LuaPlugin::from(L);
    lua_pushboolean(
        L, Server::getInstance().getCommandMap()->unregisterAlias(alias, pl) ? 1 : 0);
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
        if (p)
            p->sendMessage(msg);
    }
    return 0;
}

} // namespace

void bindCommands(lua_State* L) {
    static const luaL_Reg cmdsF[] = {{"register", l_commands_register},
                                     {"registerAlias", l_commands_register_alias},
                                     {"unregisterAlias", l_commands_unregister_alias},
                                     {"reply", l_commands_reply},
                                     {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, cmdsF, 0);
    lua_setglobal(L, "commands");
}

} // namespace cursestone::plugin
