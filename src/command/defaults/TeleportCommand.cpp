#include "TeleportCommand.h"
#include "Server.h"
#include "Player.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool TeleportCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.size() < 3) { sender->sendMessage(TC::RED + "Usage: /teleport <x> <y> <z>" + TC::RESET); return true; }
    Player* target = dynamic_cast<Player*>(sender);
    if (!target) { sender->sendMessage(TC::RED + "Console teleport not supported yet" + TC::RESET); return true; }
    double x, y, z;
    try { x = std::stod(args[0]); y = std::stod(args[1]); z = std::stod(args[2]); }
    catch (...) { sender->sendMessage(TC::RED + "Invalid coordinates" + TC::RESET); return true; }
    target->teleport(x, y, z);
    sender->sendMessage(TC::GREEN + "Teleported to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + TC::RESET);
    return true;
}
} // namespace
