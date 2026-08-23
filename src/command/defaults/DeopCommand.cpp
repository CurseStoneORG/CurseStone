#include "DeopCommand.h"
#include "Server.h"
#include "Player.h"
namespace mcppeserver {
bool DeopCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("Usage: /deop <player>"); return true; }
    Server::getInstance().setOp(args[0], false);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target) target->sendMessage("You are no longer an operator");
    sender->sendMessage("De-opped " + args[0]);
    return true;
}
} // namespace
