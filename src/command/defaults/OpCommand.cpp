#include "OpCommand.h"
#include "Server.h"
#include "Player.h"
namespace atomikpe {
bool OpCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("Usage: /op <player>"); return true; }
    Server::getInstance().setOp(args[0], true);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target) target->sendMessage("You are now an operator");
    sender->sendMessage("Opped " + args[0]);
    return true;
}
} // namespace
