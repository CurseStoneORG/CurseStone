#include "VersionCommand.h"
#include "Server.h"
#include "main.h"
#include <string>

namespace cursestone {

bool VersionCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    // Pmmp mirrored
    // Line 1: Software name + version + codename
    sender->sendMessage(std::string(CURSESTONE_NAME) + " v" + CURSESTONE_VERSION +
        " (codename: " + CURSESTONE_CODENAME + ")");
    // Line 2: Target MCPE version + protocol
    sender->sendMessage("For Minecraft PE " + std::string(MINECRAFT_VERSION) +
        ", protocol " + std::to_string(PROTOCOL_VERSION));
    // Line 3: C++ standard
    sender->sendMessage("Code: §aC++ 20 §7:: §aC23");
    // Line 4: Repo
    //sender->sendMessage("Website: github.com/CurseStoneTeam/CurseStone");
    // Line 5: Online players
    auto players = Server::getInstance().playersSnapshot();
    sender->sendMessage("Online:§a " + std::to_string(players.size()) + "§7/§r" +
        std::to_string(Server::getInstance().maxPlayers()) + " players");
    return true;
}

} // namespace
