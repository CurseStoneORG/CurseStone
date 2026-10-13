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
#include "TeleportCommand.h"
#include "Server.h"
#include "Player.h"
namespace cursestone {
bool TeleportCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sender->sendMessage("Usage: /teleport <x> <y> <z>");
        return true;
    }
    Player* target = dynamic_cast<Player*>(sender);
    if (!target) { sender->sendMessage("Console teleport not supported yet"); return true; }
    double x, y, z;
    try { x = std::stod(args[0]); y = std::stod(args[1]); z = std::stod(args[2]); }
    catch (...) { sender->sendMessage("Invalid coordinates"); return true; }
    target->teleport(x, y, z);
    sender->sendMessage("Teleported to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
    return true;
}
} // namespace
