#include "DeopCommand.h"
#include "Server.h"
#include "Player.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool DeopCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage(TC::RED + "Usage: /deop <player>" + TC::RESET); return true; }
    Server::getInstance().setOp(args[0], false);
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (target) target->sendMessage(TC::YELLOW + "You are no longer an operator" + TC::RESET);
    sender->sendMessage(TC::GREEN + "De-opped " + args[0] + TC::RESET);
    return true;
}
} // namespace
