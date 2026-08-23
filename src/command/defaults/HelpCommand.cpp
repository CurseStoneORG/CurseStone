#include "HelpCommand.h"
#include "Server.h"
namespace mcppeserver {
bool HelpCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage("\xc2\xa7e--- Showing help page ---");
    sender->sendMessage("\xc2\xa7b/help \xc2\xa77- Show this help");
    sender->sendMessage("\xc2\xa7b/gamemode <0|1|2> [player] \xc2\xa77- Change gamemode");
    sender->sendMessage("\xc2\xa7b/gm <0|1|2> \xc2\xa77- Gamemode alias");
    sender->sendMessage("\xc2\xa7b/op <player> \xc2\xa77- Give operator status");
    sender->sendMessage("\xc2\xa7b/deop <player> \xc2\xa77- Remove operator status");
    sender->sendMessage("\xc2\xa7b/list \xc2\xa77- List online players");
    sender->sendMessage("\xc2\xa7b/say <message> \xc2\xa77- Broadcast a message");
    sender->sendMessage("\xc2\xa7b/kick <player> [reason] \xc2\xa77- Kick a player");
    sender->sendMessage("\xc2\xa7b/teleport <x> <y> <z> \xc2\xa77- Teleport");
    sender->sendMessage("\xc2\xa7b/tp <x> <y> <z> \xc2\xa77- Teleport alias");
    sender->sendMessage("\xc2\xa7b/time <set|add|get> [value] \xc2\xa77- Change time");
    sender->sendMessage("\xc2\xa7b/save \xc2\xa77- Save the world");
    sender->sendMessage("\xc2\xa7b/stop \xc2\xa77- Stop the server");
    sender->sendMessage("\xc2\xa7b/give <player> <item> [count] \xc2\xa77- Give items");
    sender->sendMessage("\xc2\xa7b/weather <clear|rain> \xc2\xa77- Change weather");
    return true;
}
} // namespace
