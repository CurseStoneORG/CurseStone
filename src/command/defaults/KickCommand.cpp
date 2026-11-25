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
#include "KickCommand.h"
#include "Server.h"
#include "Player.h"
namespace cursestone {
bool KickCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendTranslatable("commands.generic.usage", "Usage: /kick <player> [reason]",
                                 {"/kick <player> [reason ...]"});
        return true;
    }
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (!target) {
        sender->sendTranslatable("commands.kick.not.found", "Player not found: " + args[0],
                                 {args[0]});
        return true;
    }
    if (args.size() >= 2) {
        std::string reason = args[1];
        for (size_t i = 2; i < args.size(); ++i)
            reason += " " + args[i];
        target->kick(reason);
        // commands.kick.success.reason = "%s ha sido expulsado: '%s'" — name,
        // then the reason (the client wraps both in its own template).
        sender->sendTranslatable("commands.kick.success.reason",
                                 "Kicked " + target->getName() + ": " + reason,
                                 {target->getName(), reason});
        return true;
    }
    target->kick("Kicked");
    sender->sendTranslatable("commands.kick.success", "Kicked " + target->getName(),
                             {target->getName()});
    return true;
}
} // namespace
