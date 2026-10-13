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
// LuaCommand — implementación del puente hilo-consola → hilo principal.
#include "LuaCommand.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <memory>
#include <string>
#include <vector>

namespace cursestone::plugin {

// Criterio de permiso (lo llama dispatch() y /help). Mismo hilo que
// dispatch: para jugadores es el hilo principal, igual que los comandos
// normales que ya usan hasPermission ahí.
bool LuaCommand::canUse(CommandSender* sender) const {
    switch (mode_) {
        case PermMode::PUBLIC: return true;
        case PermMode::OP:
            // Ops y consola pasan; un jugador normal con el nodo del
            // comando concedido explícitamente también.
            return sender->isOp() ||
                   sender->hasPermission("cursestone.command." + name());
        case PermMode::NODE:
            // El nodo custom manda; los ops son admin y pasan siempre.
            return sender->hasPermission(node_) || sender->isOp();
    }
    return false;
}

bool LuaCommand::execute(CommandSender* sender, const std::string&,
                         const std::vector<std::string>& args) {
    // 1. Copiar AHORA MISMO los datos del sender. El puntero `sender` NO
    //    se puede usar después de encolar: cuando el comando viene de la
    //    consola es una variable local del hilo CommandReader.
    //    (El chequeo de permisos ya lo hizo canUse() en dispatch().)
    std::string senderName = sender->getName();
    bool isConsole = (senderName == "CONSOLE");
    bool isOp = sender->isOp();

    // 2. Plugin deshabilitado: responder y no llamar a Lua.
    if (!plugin_->isEnabled()) {
        sender->sendMessage("That plugin is disabled.");
        return true;
    }

    // 3. Encolar la ejecución de Lua para el próximo tick (hilo principal).
    LuaPlugin* plugin = plugin_;
    int fnRef = fnRef_;
    auto task = std::make_shared<scheduler::CallbackTask>(
        [plugin, fnRef, senderName, isConsole, isOp, args]() {
            if (!plugin->isEnabled()) return;
            lua_State* L = plugin->L();

            lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);   // función

            // Tabla sender: { name, isConsole, isOp }
            lua_createtable(L, 0, 3);
            lua_pushstring(L, senderName.c_str());
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, isConsole ? 1 : 0);
            lua_setfield(L, -2, "isConsole");
            lua_pushboolean(L, isOp ? 1 : 0);
            lua_setfield(L, -2, "isOp");

            // Tabla args: lista de strings (sin el nombre del comando).
            lua_createtable(L, (int)args.size(), 0);
            for (size_t i = 0; i < args.size(); ++i) {
                lua_pushstring(L, args[i].c_str());
                lua_rawseti(L, -2, (int)(i + 1));
            }

            plugin->pcall(2, 0);                        // consume fn + 2 args
        });

    // 4. Programar con delay 0 (corre en el próximo tick) y devolver true:
    //    el comando "se aceptó", aunque el efecto sea asíncrono.
    scheduler::ServerScheduler::get().scheduleSync(task, 0, 0);
    return true;
}

}
