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
#include "TimeCommand.h"
#include "Server.h"
#include "world/World.h"
#include "Player.h"
namespace cursestone {
namespace {
// Multiworld rule for /time: the command acts on the world its sender
// stands in (console -> default world) and broadcasts ONLY there. The old
// shape always mutated the default world and pushed its time to every
// player of every world — a player in another world got their client
// clock forced onto the default world's time until their own world's next
// re-anchor, a real multiworld desync.
world::World* timeTargetWorld(CommandSender* sender) {
    if (auto* p = dynamic_cast<Player*>(sender))
        return p->getWorld();
    return Server::getInstance().getDefaultWorld();
}
} // namespace
bool TimeCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>& args) {
    if (args.empty()) {
        auto* lvl = timeTargetWorld(sender);
        if (lvl)
            sender->sendTranslatable("commands.time.query", "Current time: " +
                                        std::to_string(lvl->time()),
                                     {std::to_string(lvl->time())});
        return true;
    }
    if (args[0] == "set" && args.size() >= 2) {
        int32_t t;
        // "night" = World::TIME_NIGHT (14000), exactly what the reference server
        // do (`case "night"` -> World::TIME_NIGHT). It used to be
        // 13000, which the client renders as dark BUT the sleep check
        // rejects (a bed tapped at 13000 answers "You can only sleep at
        // night" — the classic "it looks like night but the bed says no"
        // trap). Aliasing the command to TIME_NIGHT keeps the command and
        // the bed check on the same definition of night.
        if (args[1] == "day")
            t = 1000;
        else if (args[1] == "night")
            t = world::World::TIME_NIGHT;
        else {
            try {
                t = std::stoi(args[1]);
            } catch (...) {
                sender->sendTranslatable("commands.generic.num.invalid",
                                         "'" + args[1] + "' is not a valid number",
                                         {args[1]});
                return true;
            }
        }
        auto* lvl = timeTargetWorld(sender);
        if (lvl) {
            lvl->setTime(t);
            // One scoped broadcast: exactly the viewers of this world, and
            // the packet is built once (or not at all for an empty world).
            lvl->broadcastTimeSync();
            sender->sendTranslatable("commands.time.set", "Time set to " + std::to_string(t),
                                     {std::to_string(t)});
        }
        return true;
    }
    if (args[0] == "add" && args.size() >= 2) {
        int32_t t;
        try {
            t = std::stoi(args[1]);
        } catch (...) {
            sender->sendTranslatable("commands.generic.num.invalid",
                                     "'" + args[1] + "' is not a valid number",
                                     {args[1]});
            return true;
        }
        auto* lvl = timeTargetWorld(sender);
        if (lvl) {
            lvl->advanceTime(t);
            // The client keeps interpolating from its last anchor, so the
            // shift is invisible until the next re-anchor without this.
            lvl->broadcastTimeSync();
            sender->sendTranslatable("commands.time.added", "Added " + std::to_string(t) +
                                        " to time",
                                     {std::to_string(t)});
        }
        return true;
    }
    sender->sendTranslatable("commands.generic.usage", "Usage: /time <set|add|get> [value]",
                             {"/time <set|add|query> <value>"});
    return true;
}
} // namespace
