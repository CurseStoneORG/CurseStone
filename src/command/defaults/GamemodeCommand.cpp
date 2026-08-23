#include "GamemodeCommand.h"
#include "Server.h"
#include "Player.h"
#include "network/protocol/SetPlayerGameTypePacket.h"
#include "network/protocol/AdventureSettingsPacket.h"
#include <algorithm>

namespace mcppeserver {
bool GamemodeCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendMessage("§cUsage: /gamemode <0|1|2> [player]");
        return true;
    }

    int gm = -1;
    // Accept numeric or name
    if (args[0] == "0" || args[0] == "s" || args[0] == "survival") gm = 0;
    else if (args[0] == "1" || args[0] == "c" || args[0] == "creative") gm = 1;
    else if (args[0] == "2" || args[0] == "a" || args[0] == "adventure") gm = 2;
    else {
        try { gm = std::stoi(args[0]); } catch (...) {}
    }
    if (gm < 0 || gm > 2) {
        sender->sendMessage("§cInvalid gamemode: " + args[0]);
        return true;
    }

    Player* target = nullptr;
    // If second arg, find that player
    if (args.size() >= 2) {
        target = Server::getInstance().getPlayer(args[1]);
        if (!target) {
            sender->sendMessage("§cPlayer not found: " + args[1]);
            return true;
        }
    } else {
        // Try to cast sender to Player
        target = dynamic_cast<Player*>(sender);
        if (!target) {
            sender->sendMessage("§cConsole must specify a player: /gamemode " + args[0] + " <player>");
            return true;
        }
    }

    target->setGamemode(gm);

    // Send SetPlayerGameTypePacket to the target
    auto sg = std::make_shared<network::protocol::SetPlayerGameTypePacket>();
    sg->gamemode = gm & 0x01;  // client only cares about survival vs creative
    target->sendPacket(sg);

    // Send AdventureSettingsPacket (flight flags)
    auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
    uint32_t flags = 0;
    if (gm == 1 || gm == 3) flags |= network::protocol::AdventureSettingsPacket::FLAG_ALLOW_FLIGHT;
    as->flags = flags;
    target->sendPacket(as);

    std::string gmName = gm == 0 ? "Survival" : (gm == 1 ? "Creative" : "Adventure");
    sender->sendMessage("§aSet " + target->getName() + "'s game mode to " + gmName);
    return true;
}
} // namespace
