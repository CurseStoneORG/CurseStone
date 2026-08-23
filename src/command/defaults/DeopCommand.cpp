#include "DeopCommand.h"
#include "Server.h"
#include "Player.h"
namespace mcppeserver {
bool DeopCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("\xc2\xa7cUsage: /deop <player>"); return true; }
    Server::getInstance().setOp(args[0], false);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target) target->sendMessage("\xc2\xa7eYou are no longer an operator");
    sender->sendMessage("\xc2\xa7aDe-opped " + args[0]);
    return true;
}
} // namespace
