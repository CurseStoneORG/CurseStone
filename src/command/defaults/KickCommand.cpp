#include "KickCommand.h"
#include "Server.h"
#include "Player.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool KickCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage(TC::RED + "Usage: /kick <player> [reason]" + TC::RESET); return true; }
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (!target) { sender->sendMessage(TC::RED + "Player not found: " + args[0] + TC::RESET); return true; }
    std::string reason = "Kicked";
    if (args.size() >= 2) { reason = args[1]; for (size_t i = 2; i < args.size(); ++i) reason += " " + args[i]; }
    target->kick(reason);
    sender->sendMessage(TC::GREEN + "Kicked " + target->getName() + ": " + reason + TC::RESET);
    return true;
}
} // namespace
