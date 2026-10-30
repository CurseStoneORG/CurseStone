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
// LuaCommand — implementation of the console-thread -> main-thread bridge.
#include "LuaCommand.h"
#include "PluginManager.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <memory>
#include <string>
#include <vector>

namespace cursestone::plugin {

// Criterio of permiso (it flame dispatch() and /help). same thread that
// dispatch: for players is the thread main, just like the commands
// normales that already use hasPermission there.
bool LuaCommand::canUse(CommandSender* sender) const {
    switch (mode_) {
    case PermMode::PUBLIC:
        return true;
    case PermMode::OP:
        // ops and consola pass; a player normal with the node of the
        // command granted explicitly also.
        return sender->isOp() || sender->hasPermission("cursestone.command." + name());
    case PermMode::NODE:
        // the node custom takes precedence; the ops are admin and pass always.
        return sender->hasPermission(node_) || sender->isOp();
    }
    return false;
}

bool LuaCommand::execute(CommandSender* sender, const std::string&,
                         const std::vector<std::string>& args) {
    // 1. Copiar now same the data of the sender. the pointer `sender` not
    //    is can use after enqueue: when the command comes of the
    //    consola is a variable local of the thread CommandReader.
    //    (the chequeo of permisos already it hizo canUse() in dispatch().)
    std::string senderName = sender->getName();
    bool isConsole = (senderName == "CONSOLE");
    bool isOp = sender->isOp();

    // 2. plugin deshabilitado: responder and not call to Lua.
    if (!plugin_->isEnabled()) {
        sender->sendMessage("That plugin is disabled.");
        return true;
    }

    // 3. enqueue the execution of Lua for the next tick (thread main).
    //    not is captures the pointer LuaPlugin*: between this tick and the
    //    next a /plugins reload can destroy the object (and its
    //    lua_State) — keeping it here era a use-after-free. is captures
    //    the name and the ref of the function; in the next tick is
    //    resolves contra PluginManager::find() and is checks that the
    //    ref siga being a function of the state VIVO of the plugin.
    std::string pluginName = plugin_->name();
    int fnRef = fnRef_;
    auto task = std::make_shared<scheduler::CallbackTask>(
        [pluginName, fnRef, senderName, isConsole, isOp, args]() {
            LuaPlugin* plugin = PluginManager::get().find(pluginName);
            if (!plugin || !plugin->isEnabled())
                return;
            lua_State* L = plugin->L();

            lua_pushvalue(L, LUA_REGISTRYINDEX);
            lua_rawgeti(L, -1, fnRef);
            bool haveFn = lua_isfunction(L, -1);
            lua_remove(L, -2); // quita the tabla registry, deja fn/nil
            if (!haveFn) {     // ref of a state anterior (tras reload)
                lua_pop(L, 1);
                return;
            }

            // Tabla sender: { name, isConsole, isOp }
            lua_createtable(L, 0, 3);
            lua_pushstring(L, senderName.c_str());
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, isConsole ? 1 : 0);
            lua_setfield(L, -2, "isConsole");
            lua_pushboolean(L, isOp ? 1 : 0);
            lua_setfield(L, -2, "isOp");

            // Tabla args: list of strings (without the name of the command).
            lua_createtable(L, (int)args.size(), 0);
            for (size_t i = 0; i < args.size(); ++i) {
                lua_pushstring(L, args[i].c_str());
                lua_rawseti(L, -2, (int)(i + 1));
            }

            plugin->pcall(2, 0); // consume fn + 2 args
        });

    // 4. schedule with delay 0 (runs in the next tick) and return true:
    //    the command "was accepted", although the effect is asynchronous.
    scheduler::ServerScheduler::get().scheduleSync(task, 0, 0);
    return true;
}

}
