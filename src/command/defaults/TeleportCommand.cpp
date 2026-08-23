#include "TeleportCommand.h"
#include "Server.h"
#include "Player.h"
namespace mcppeserver {
bool TeleportCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sender->sendMessage("\xc2\xa7cUsage: /teleport <x> <y> <z>");
        return true;
    }
    Player* target = dynamic_cast<Player*>(sender);
    if (!target) { sender->sendMessage("\xc2\xa7cConsole teleport not supported yet"); return true; }
    double x, y, z;
    try { x = std::stod(args[0]); y = std::stod(args[1]); z = std::stod(args[2]); }
    catch (...) { sender->sendMessage("\xc2\xa7cInvalid coordinates"); return true; }
    target->teleport(x, y, z);
    sender->sendMessage("\xc2\xa7aTeleported to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
    return true;
}
} // namespace
