#include "OpCommand.h"
#include "Server.h"
#include "Player.h"

namespace mcppeserver {
bool OpCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendMessage("§cUsage: /op <player>");
        return true;
    }
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (!target) {
        sender->sendMessage("§cPlayer not found: " + args[0]);
        return true;
    }
    target->setOp(true);
    sender->sendMessage("§aOpped " + target->getName());
    target->sendMessage("§eYou are now an operator");
    return true;
}
} // namespace
