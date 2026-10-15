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
// PluginsCommand — implementación.
//
//   /plugins            -> lista los plugins (solo op/consola)
//   /plugins reload     -> recarga TODOS los plugins (solo op/consola)
//
// HILOS: los comandos de consola corren en el hilo del CommandReader y
// Lua NO es thread-safe, así que el reload NO toca los lua_States aquí:
// se copian los datos del sender (el puntero NO sobrevive: en consola es
// una variable local del CommandReader), se encola un CallbackTask con
// ServerScheduler::scheduleSync y PluginManager::reloadAll() corre en el
// próximo tick, en el hilo principal. El resultado se reporta en el log.
#include "PluginsCommand.h"
#include "plugin/PluginManager.h"
#include "plugin/LuaBindings.h" // CURSESTONE_API
#include "Server.h"
#include "Player.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace cursestone {

bool PluginsCommand::execute(CommandSender* sender, const std::string&,
                             const std::vector<std::string>& args) {
    // ---- /plugins reload: op/consola, ejecutado en el hilo principal ----
    if (!args.empty()) {
        std::string sub = args[0];
        std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });

        if (sub == "reload") {
            if (!sender->isOp()) {
                sender->sendMessage("You do not have permission to use this command.");
                return false;
            }
            // Copiar el nombre AHORA (el puntero sender no sobrevive si el
            // comando vino de la consola) y puentear al hilo principal.
            std::string senderName = sender->getName();
            auto task = std::make_shared<scheduler::CallbackTask>([senderName]() {
                size_t n = plugin::PluginManager::get().reloadAll();
                // Si lo pidió un jugador conectado, confirmarle por chat
                // (la consola ya ve todo el log del reload).
                if (Player* p = Server::getInstance().getPlayerExact(senderName))
                    p->sendMessage("Reloaded " + std::to_string(n) + " plugin(s).");
            });
            scheduler::ServerScheduler::get().scheduleSync(task, 0, 0);
            sender->sendMessage("Reloading plugins... (check the log for details)");
            return true;
        }

        sender->sendMessage("Usage: /plugins [reload]");
        return false;
    }

    // ---- /plugins a secas: lista ----
    auto list = plugin::PluginManager::get().list();
    if (list.empty()) {
        sender->sendMessage("No plugins loaded. (API " + std::string(CURSESTONE_API) + ")");
        return true;
    }
    std::string out =
        "Plugins (API " + std::string(CURSESTONE_API) + ") (" + std::to_string(list.size()) + "): ";
    for (size_t i = 0; i < list.size(); ++i) {
        if (i)
            out += ", ";
        out += list[i].name + " v" + list[i].version;
        if (!list[i].author.empty())
            out += " by " + list[i].author;
        if (!list[i].enabled)
            out += " (disabled)";
    }
    sender->sendMessage(out);
    return true;
}

}
