#include "KickCommand.h"
#include "Server.h"
#include "Player.h"
namespace mcppeserver {
bool KickCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("\xc2\xa7cUsage: /kick <player> [reason]"); return true; }
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (!target) { sender->sendMessage("\xc2\xa7cPlayer not found: " + args[0]); return true; }
    std::string reason = "Kicked";
    if (args.size() >= 2) { reason = args[1]; for (size_t i = 2; i < args.size(); ++i) reason += " " + args[i]; }
    target->kick(reason);
    sender->sendMessage("\xc2\xa7aKicked " + target->getName() + ": " + reason);
    return true;
}
} // namespace
