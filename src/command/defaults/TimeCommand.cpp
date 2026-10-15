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
#include "TimeCommand.h"
#include "Server.h"
#include "world/World.h"
#include "network/protocol/SetTimePacket.h"
#include "Player.h"
namespace cursestone {
bool TimeCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) {
        auto* lvl = Server::getInstance().getDefaultWorld();
        if (lvl) sender->sendMessage("Current time: " + std::to_string(lvl->time()));
        return true;
    }
    if (args[0] == "set" && args.size() >= 2) {
        int32_t t;
        // "night" = World::TIME_NIGHT (14000), exactly what PMMP/Infinity
        // do (`case "night": $value = Level::TIME_NIGHT;`). It used to be
        // 13000, which the client renders as dark BUT the sleep check
        // rejects (a bed tapped at 13000 answers "You can only sleep at
        // night" — the classic "it looks like night but the bed says no"
        // trap). Aliasing the command to TIME_NIGHT keeps the command and
        // the bed check on the same definition of night.
        if (args[1] == "day") t = 1000;
        else if (args[1] == "night") t = world::World::TIME_NIGHT;
        else { try { t = std::stoi(args[1]); } catch (...) { sender->sendMessage("Invalid time"); return true; } }
        auto* lvl = Server::getInstance().getDefaultWorld();
        if (lvl) {
            lvl->setTime(t);
            auto pk = std::make_shared<network::protocol::SetTimePacket>();
            pk->time = t; pk->started = true;
            for (auto& p : Server::getInstance().playersSnapshot()) { if (p) p->sendPacket(pk); }
            sender->sendMessage("Set time to " + std::to_string(t));
        }
        return true;
    }
    if (args[0] == "add" && args.size() >= 2) {
        int32_t t; try { t = std::stoi(args[1]); } catch (...) { sender->sendMessage("Invalid value"); return true; }
        auto* lvl = Server::getInstance().getDefaultWorld();
        if (lvl) { lvl->advanceTime(t); sender->sendMessage("Added " + std::to_string(t) + " to time"); }
        return true;
    }
    sender->sendMessage("Usage: /time <set|add|get> [value]");
    return true;
}
} // namespace
