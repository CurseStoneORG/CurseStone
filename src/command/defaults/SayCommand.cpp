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
#include "SayCommand.h"
#include "Server.h"
namespace cursestone {
bool SayCommand::execute(CommandSender* sender, const std::string&,
                         const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendTranslatable("commands.generic.usage", "Usage: /say <message>",
                                 {"/say <message ...>"}, TC::RED);
        return true;
    }
    std::string msg;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0)
            msg += " ";
        msg += args[i];
    }
    // The client renders announcements from its own lang table
    // (chat.type.announcement = "[%s] %s"); the console keeps the plain line.
    Server::getInstance().broadcastTranslation(
        "chat.type.announcement", {sender->getName(), msg},
        "[" + sender->getName() + "] " + msg);
    return true;
}
} // namespace
