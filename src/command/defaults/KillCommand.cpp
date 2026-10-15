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

#include "KillCommand.h"
#include "Server.h"
#include "Player.h"

namespace cursestone {

bool KillCommand::execute(
    CommandSender* sender,
    const std::string&,
    const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendMessage("Usage: /kill <player>");
        return true;
    }

    Player* target = Server::getInstance().getPlayer(args[0]);

    if (!target) {
        sender->sendMessage("Player not found: " + args[0]);
        return true;
    }

    if (target->isDead()) {
        sender->sendMessage(target->getName() + " is already dead");
        return true;
    }

    // Regular death pipeline (Player::setHealth(0) -> applyDamage ->
    // onDeath): death screen for the client, drops, PlayerDeathEvent and
    // the "<name> died" broadcast. Creative players die too, like vanilla.
    target->setHealth(0);

    sender->sendMessage("Killed " + target->getName());

    return true;
}

} // namespace cursestone