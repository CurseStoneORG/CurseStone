#include "HelpCommand.h"
#include "Server.h"
namespace mcppeserver {
bool HelpCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage("--- Showing help page ---");
    sender->sendMessage("/help - Show this help");
    sender->sendMessage("/gamemode <0|1|2> [player] - Change gamemode");
    sender->sendMessage("/gm <0|1|2> - Gamemode alias");
    sender->sendMessage("/op <player> - Give operator status");
    sender->sendMessage("/deop <player> - Remove operator status");
    sender->sendMessage("/list - List online players");
    sender->sendMessage("/say <message> - Broadcast a message");
    sender->sendMessage("/kick <player> [reason] - Kick a player");
    sender->sendMessage("/teleport <x> <y> <z> - Teleport");
    sender->sendMessage("/tp <x> <y> <z> - Teleport alias");
    sender->sendMessage("/time <set|add|get> [value] - Change time");
    sender->sendMessage("/save - Save the world");
    sender->sendMessage("/stop - Stop the server");
    sender->sendMessage("/give <player> <item> [count] - Give items");
    sender->sendMessage("/weather <clear|rain> - Change weather");
    return true;
}
} // namespace
