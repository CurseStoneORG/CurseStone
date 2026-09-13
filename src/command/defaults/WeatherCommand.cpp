#include "WeatherCommand.h"
#include "Server.h"
#include "level/Level.h"
#include "Player.h"

// /weather <clear|rain|thunder> [duration in seconds]
//
// Mirrors vanilla/PMMP semantics:
//   clear   -> stops rain (and thunder with it)
//   rain    -> starts rain, stops thunder
//   thunder -> starts rain + thunder
//
// The optional duration is given in SECONDS (like vanilla /weather),
// converted to ticks at 20 TPS server-side. Without a duration the
// weather stays until changed again.
namespace atomikpe {

bool WeatherCommand::execute(CommandSender* sender, const std::string& label,
                    const std::vector<std::string>& args) {
    (void)label;
    auto* lvl = Server::getInstance().getDefaultLevel();
    if (!lvl) {
        sender->sendMessage("No default level loaded");
        return true;
    }

    if (args.empty()) {
        sender->sendMessage("Usage: /weather <clear|rain|thunder> [duration in seconds]");
        sender->sendMessage("Current: " + std::string(lvl->isRaining()
            ? (lvl->isThundering() ? "thunder" : "rain") : "clear"));
        return true;
    }

    // Duration: seconds -> ticks (20 TPS). 0 / omitted = forever.
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
    if (w == "clear" || w == "0") {
        lvl->setThundering(false);
        lvl->setRaining(false);
        sender->sendMessage("Weather cleared");
    } else if (w == "rain" || w == "1") {
        lvl->setThundering(false);
        lvl->setRaining(true, duration_ticks);
        sender->sendMessage("Rain started" +
            std::string(duration_ticks > 0 ? " (" + std::to_string(duration_ticks / 20) + "s)" : ""));
    } else if (w == "thunder" || w == "2") {
        lvl->setThundering(true, duration_ticks);
        sender->sendMessage("Thunderstorm started" +
            std::string(duration_ticks > 0 ? " (" + std::to_string(duration_ticks / 20) + "s)" : ""));
    } else {
        sender->sendMessage("Usage: /weather <clear|rain|thunder> [duration in seconds]");
    }
    return true;
}

} // namespace atomikpe
