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
#include "WeatherCommand.h"
#include "Server.h"
#include "world/World.h"
#include "Player.h"

// /weather <clear|rain|thunder> [duration]
//
// Ported from Dr1xyDev/Infinity WeatherCommand.php (the reference for
// MCPE 0.15.10): state ids are 0=clear, 1=rain, 2=rainy_thunder,
// 3=thunder, and the duration argument is in SECONDS (Infinity sends
// it in ticks, but seconds is what vanilla /weather uses and what a
// human types). Without a duration the state lasts until changed
// again — Infinity would instead roll a random 6000-12000 tick cycle;
// we only do that for the auto-clear below, not for manual use.
//
// NOTE: we accept this command from BOTH the console and players. The
// console variant ignores the optional world-name argument Infinity
// supports because CurseStone currently has exactly one world.
namespace cursestone {

bool WeatherCommand::execute(CommandSender* sender, const std::string& label,
                    const std::vector<std::string>& args) {
    (void)label;
    auto* lvl = Server::getInstance().getDefaultWorld();
    if (!lvl) {
        sender->sendMessage("No default world loaded");
        return true;
    }

    if (args.empty()) {
        sender->sendMessage("Usage: /weather <clear|rain|thunder> [duration in seconds]");
        const char* names[4] = { "clear", "rain", "rain+thunder", "thunder" };
        sender->sendMessage(std::string("Current: ") +
            names[lvl->weatherState() & 3] +
            (lvl->weatherTicksLeft() > 0
                ? " (" + std::to_string(lvl->weatherTicksLeft() / 20) + "s left)"
                : ""));
        return true;
    }

    // Duration: seconds -> ticks (20 TPS). 0 / omitted = until changed.
    int32_t duration_ticks = 0;
    if (args.size() >= 2) {
        try {
            int seconds = std::stoi(args[1]);
            if (seconds < 0) {
                sender->sendMessage("Invalid duration");
                return true;
            }
            duration_ticks = seconds * 20;
        } catch (...) {
            sender->sendMessage("Invalid duration");
            return true;
        }
    }

    const std::string& w = args[0];
    if (w == "clear" || w == "sunny" || w == "fine" || w == "0") {
        lvl->setWeather(world::World::WEATHER_CLEAR, duration_ticks);
        sender->sendMessage("Weather cleared");
    } else if (w == "rain" || w == "rainy" || w == "1") {
        lvl->setWeather(world::World::WEATHER_RAIN, duration_ticks);
        sender->sendMessage("Rain started" +
            std::string(duration_ticks > 0 ? " (" + std::to_string(duration_ticks / 20) + "s)" : ""));
    } else if (w == "storm" || w == "rain_thunder" || w == "rainy_thunder" || w == "2") {
        lvl->setWeather(world::World::WEATHER_RAINY_THUNDER, duration_ticks);
        sender->sendMessage("Thunderstorm started" +
            std::string(duration_ticks > 0 ? " (" + std::to_string(duration_ticks / 20) + "s)" : ""));
    } else if (w == "thunder" || w == "3") {
        // Infinity's bare "thunder" is state 3: thunder WITHOUT rain.
        lvl->setWeather(world::World::WEATHER_THUNDER, duration_ticks);
        sender->sendMessage("Thunder started" +
            std::string(duration_ticks > 0 ? " (" + std::to_string(duration_ticks / 20) + "s)" : ""));
    } else {
        sender->sendMessage("Usage: /weather <clear|rain|thunder> [duration in seconds]");
    }
    return true;
}

} // namespace cursestone
