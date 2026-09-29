#include "GamemodeCommand.h"
#include "Server.h"
#include "Player.h"
#include "network/protocol/SetPlayerGameTypePacket.h"
#include "network/protocol/AdventureSettingsPacket.h"
#include <string>
namespace cursestone {
bool GamemodeCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) {
        sender->sendMessage("Usage: /gamemode <0|1|2> [player]");
        return true;
    }
    int gm = -1;
    if (args[0] == "0" || args[0] == "s" || args[0] == "survival") gm = 0;
    else if (args[0] == "1" || args[0] == "c" || args[0] == "creative") gm = 1;
    else if (args[0] == "2" || args[0] == "a" || args[0] == "adventure") gm = 2;
    else { try { gm = std::stoi(args[0]); } catch (...) {} }
    if (gm < 0 || gm > 2) {
        sender->sendMessage("Invalid gamemode: " + args[0]);
        return true;
    }
    Player* target = nullptr;
    if (args.size() >= 2) {
        target = Server::getInstance().getPlayer(args[1]);
        if (!target) { sender->sendMessage("Player not found: " + args[1]); return true; }
    } else {
        target = dynamic_cast<Player*>(sender);
        if (!target) { sender->sendMessage("Console must specify a player: /gamemode " + args[0] + " <player>"); return true; }
    }
    target->setGamemode(gm);
    auto sg = std::make_shared<network::protocol::SetPlayerGameTypePacket>();
    sg->gamemode = gm & 0x01;
    target->sendPacket(sg);
    // Send AdventureSettings with correct flags (mirror PMMP sendSettings)
    auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
    uint32_t flags = 0;
    if (gm == 2 || gm == 3) flags |= 0x01;  // world_immutable (adventure)
    flags |= 0x02;  // no_pvp
    flags |= 0x04;  // no_pvm
    flags |= 0x08;  // no_mvp
    flags |= 0x40;  // auto_jump
    if (gm == 1 || gm == 3) flags |= 0x80;  // allow_fly
    if (gm == 3) flags |= 0x100;  // noclip
    as->flags = flags;
    as->userPermission = 2;   // host
    as->globalPermission = 2; // host
    target->sendPacket(as);
    std::string gmName = gm == 0 ? "Survival" : (gm == 1 ? "Creative" : "Adventure");
    sender->sendMessage("Set " + target->getName() + "'s game mode to " + gmName);
    return true;
}
} // namespace
