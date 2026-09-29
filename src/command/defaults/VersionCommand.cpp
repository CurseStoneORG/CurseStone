#include "VersionCommand.h"
#include "Server.h"
#include "main.h"
#include <string>

namespace cursestone {

bool VersionCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    // PMMP-style version output
    // Line 1: Software name + version + codename
    sender->sendMessage(std::string(CURSESTONE_NAME) + " v" + CURSESTONE_VERSION +
        " (codename: " + CURSESTONE_CODENAME + ")");
    // Line 2: Target MCPE version + protocol
    sender->sendMessage("Target: Minecraft PE " + std::string(MINECRAFT_VERSION) +
        ", protocol " + std::to_string(PROTOCOL_VERSION));
    // Line 3: C++ standard
    sender->sendMessage("Built with: C++20 / C23");
    // Line 4: Website
    sender->sendMessage("Website: github.com/Dr1xyDev/CurseStone");
    // Line 5: Online players
    auto players = Server::getInstance().playersSnapshot();
    sender->sendMessage("Online: " + std::to_string(players.size()) + "/" +
        std::to_string(Server::getInstance().maxPlayers()) + " players");
    return true;
}

} // namespace
