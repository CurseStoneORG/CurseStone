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
#include "ListCommand.h"
#include "Server.h"
#include "Player.h"
namespace cursestone {
bool ListCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>&) {
    const auto& players = Server::getInstance().playersSnapshotRef();
    // commands.players.list = "Hay %d/%d jugadores conectados:" — the
    // client slots the counts into its own template; the console keeps the
    // English line.
    sender->sendTranslatable(
        "commands.players.list",
        "There are " + std::to_string(players.size()) + "/" +
            std::to_string(Server::getInstance().maxPlayers()) + " players online:",
        {std::to_string(players.size()), std::to_string(Server::getInstance().maxPlayers())});
    for (auto& p : players) {
        if (p)
            sender->sendMessage("  - " + p->getName());
    }
    return true;
}
} // namespace
