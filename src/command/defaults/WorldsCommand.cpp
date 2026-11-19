/*
 _____                     _____ _
/  __ \                   /  ___| |
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
#include "WorldsCommand.h"
#include "Server.h"
#include "Player.h"
#include "world/World.h"
#include <cstdlib>

namespace cursestone {
bool WorldsCommand::execute(CommandSender* sender, const std::string&,
                            const std::vector<std::string>& args) {
    Server& srv = Server::getInstance();
    std::string sub = args.empty() ? "list" : args[0];

    // /worlds (or /worlds list): one line per world, default world first.
    if (sub == "list") {
        auto worlds = srv.getWorlds();
        sender->sendMessage("Loaded worlds: " + std::to_string(worlds.size()));
        for (World* w : worlds) {
            if (!w)
                continue;
            bool is_default = srv.getDefaultWorld() == w;
            int players = 0;
            for (const auto& p : srv.playersSnapshotRef()) {
                if (p && p->getWorld() == w)
                    ++players;
            }
            sender->sendMessage(" - " + w->name() +
                                (is_default ? " [default]" : "") +
                                " dim=" + std::to_string(w->dimension()) +
                                " gen=" + w->config().generator +
                                " players=" + std::to_string(players) +
                                " chunks=" + std::to_string(w->chunkCount()));
        }
        return true;
    }

    if (sub == "tp") {
        if (args.size() < 2) {
            sender->sendMessage("Usage: /worlds tp <world>");
            return true;
        }
        Player* p = dynamic_cast<Player*>(sender);
        if (!p) {
            sender->sendMessage("Only players can switch worlds");
            return true;
        }
        World* target = srv.getWorldByName(args[1]);
        if (!target) {
            sender->sendMessage("World '" + args[1] + "' is not loaded");
            return true;
        }
        auto sp = target->spawn();
        if (p->switchWorld(target, sp.x, sp.y, sp.z, false))
            sender->sendMessage("Switched to '" + target->name() + "'");
        else
            sender->sendMessage("Switch to '" + target->name() + "' was cancelled");
        return true;
    }

    if (sub == "load") {
        if (args.size() < 2) {
            sender->sendMessage("Usage: /worlds load <name> [generator] [seed]");
            return true;
        }
        int64_t seed = 0;
        if (args.size() >= 4) {
            try {
                seed = std::stoll(args[3]);
            } catch (...) {
                sender->sendMessage("Invalid seed '" + args[3] + "'");
                return true;
            }
        }
        World* w = srv.loadWorld(args[1], args.size() >= 3 ? args[2] : "normal", seed);
        if (w)
            sender->sendMessage("World '" + w->name() + "' loaded (dim=" +
                                std::to_string(w->dimension()) + ")");
        else
            sender->sendMessage("Could not load '" + args[1] +
                                "' (name/generator rejected or already loaded)");
        return true;
    }

    if (sub == "unload") {
        if (args.size() < 2) {
            sender->sendMessage("Usage: /worlds unload <name>");
            return true;
        }
        sender->sendMessage(srv.unloadWorld(args[1])
                                ? "World '" + args[1] + "' unloaded"
                                : "Could not unload '" + args[1] + "'");
        return true;
    }

    sender->sendMessage("Usage: /worlds [list | tp <world> | load <name> [gen] [seed] | "
                        "unload <name>]");
    return true;
}
} // namespace cursestone
