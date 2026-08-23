#include "HelpCommand.h"
#include "Server.h"
#include "command/CommandMap.h"
namespace mcppeserver {
bool HelpCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("§6--- Showing help page ---");
    auto* map = Server::getInstance().getCommandMap();
    if (!map) return true;
    // SimpleCommandMap doesn't expose iteration; list the well-known ones.
    sender->sendMessage("§e/help §7- Show this help");
    sender->sendMessage("§e/gamemode <0|1|2> [player] §7- Change gamemode");
    sender->sendMessage("§e/gm <0|1|2> §7- Gamemode alias");
    sender->sendMessage("§e/op <player> §7- Give operator status");
    sender->sendMessage("§e/deop <player> §7- Remove operator status");
    sender->sendMessage("§e/list §7- List online players");
    sender->sendMessage("§e/say <message> §7- Broadcast a message");
    sender->sendMessage("§e/kick <player> [reason] §7- Kick a player");
    sender->sendMessage("§e/teleport <x> <y> <z> §7- Teleport");
    sender->sendMessage("§e/tp <x> <y> <z> §7- Teleport alias");
    sender->sendMessage("§e/time <set|add|get> [value] §7- Change time");
    sender->sendMessage("§e/save §7- Save the world");
    sender->sendMessage("§e/stop §7- Stop the server");
    sender->sendMessage("§e/give <player> <item> [count] §7- Give items");
    sender->sendMessage("§e/weather <clear|rain|thunder> §7- Change weather");
    return true;
}
} // namespace
