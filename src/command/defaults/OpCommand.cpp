#include "OpCommand.h"
#include "Server.h"
#include "Player.h"
namespace mcppeserver {
bool OpCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("\xc2\xa7cUsage: /op <player>"); return true; }
    Server::getInstance().setOp(args[0], true);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target) target->sendMessage("\xc2\xa7eYou are now an operator");
    sender->sendMessage("\xc2\xa7aOpped " + args[0]);
    return true;
}
} // namespace
