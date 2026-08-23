#include "DeopCommand.h"
#include "Server.h"
#include "Player.h"

namespace mcppeserver {
bool DeopCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendMessage("§cUsage: /deop <player>");
        return true;
    }
    Player* target = Server::getInstance().getPlayer(args[0]);
    if (!target) {
        sender->sendMessage("§cPlayer not found: " + args[0]);
        return true;
    }
    target->setOp(false);
    sender->sendMessage("§aDe-opped " + target->getName());
    target->sendMessage("§eYou are no longer an operator");
    return true;
}
} // namespace
