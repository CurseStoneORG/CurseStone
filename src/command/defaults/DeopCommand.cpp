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
#include "utils/TextColor.h"
#include "DeopCommand.h"
#include "Server.h"
#include "Player.h"
namespace cursestone {
bool DeopCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendTranslatable("commands.generic.usage", "Usage: /deop <player>",
                                 {"/deop <player>"}, TC::RED);
        return true;
    }
    Server::getInstance().setOp(args[0], false);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target)
        target->sendTranslation("commands.deop.message");
    sender->sendTranslatable("commands.deop.success", "De-opped " + args[0], {args[0]});
    return true;
}
} // namespace
