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

#include "VersionCommand.h"
#include "Server.h"
#include "CurseStone.h"
#include <string>

namespace cursestone {

bool VersionCommand::execute(CommandSender* sender, const std::string&,
                             const std::vector<std::string>&) {
    // The reference server mirrored
    // Line 1: Software name + version + codename
    sender->sendMessage(std::string(CURSESTONE_NAME) + " v" + CURSESTONE_API +
                        " (codename: " + CURSESTONE_CODENAME + ")");
    // Line 2: Target MCPE version + protocol
    sender->sendMessage("For Minecraft PE " + std::string(MINECRAFT_VERSION) + ", protocol " +
                        std::to_string(PROTOCOL_VERSION));
    // Line 3: C++ standard
    sender->sendMessage("Code: §aC++ 20 §7:: §aC23");
    // Line 4: Repo
    //sender->sendMessage("Website: github.com/CurseStoneTeam/CurseStone");
    // Line 5: Online players
    auto players = Server::getInstance().playersSnapshot();
    sender->sendMessage("Online:§a " + std::to_string(players.size()) + "§7/§r" +
                        std::to_string(Server::getInstance().maxPlayers()) + " players");
    return true;
}

} // namespace
