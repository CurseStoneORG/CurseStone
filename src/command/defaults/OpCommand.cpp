#include "OpCommand.h"
#include "Server.h"
#include "Player.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool OpCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage(TC::RED + "Usage: /op <player>" + TC::RESET); return true; }
    Server::getInstance().setOp(args[0], true);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target) target->sendMessage(TC::YELLOW + "You are now an operator" + TC::RESET);
    sender->sendMessage(TC::GREEN + "Opped " + args[0] + TC::RESET);
    return true;
}
} // namespace
