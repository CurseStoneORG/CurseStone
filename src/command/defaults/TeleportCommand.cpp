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
#include "TeleportCommand.h"
#include "Server.h"
#include "Player.h"
namespace cursestone {
bool TeleportCommand::execute(CommandSender* sender, const std::string&,
                              const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sender->sendTranslatable("commands.generic.usage", "Usage: /teleport <x> <y> <z>",
                                 {"/teleport <x> <y> <z>"}, TC::RED);
        return true;
    }
    Player* target = dynamic_cast<Player*>(sender);
    if (!target) {
        sender->sendMessage("Console teleport not supported yet");
        return true;
    }
    double coords[3];
    for (int i = 0; i < 3; ++i) {
        try {
            coords[i] = std::stod(args[i]);
        } catch (...) {
            // The offending token rides as the %s of the client's
            // "'%s' is not a valid number" template.
            sender->sendTranslatable("commands.generic.num.invalid",
                                     "'" + args[i] + "' is not a valid number", {args[i]}, TC::RED);
            return true;
        }
    }
    target->teleport(coords[0], coords[1], coords[2]);
    // commands.tp.success.coordinates = "%s ha sido teletransportado a %s, %s, %s"
    // — victim name first, then the coordinates.
    std::string ax = std::to_string(coords[0]), ay = std::to_string(coords[1]),
                az = std::to_string(coords[2]);
    sender->sendTranslatable(
        "commands.tp.success.coordinates",
        "Teleported to " + ax + ", " + ay + ", " + az,
        {target->getName(), ax, ay, az});
    return true;
}
} // namespace
