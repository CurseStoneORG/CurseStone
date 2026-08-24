#include "VersionCommand.h"
#include "Server.h"
#include "main.h"
#include <string>

namespace atomikpe {

bool VersionCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    // PMMP-style version output
    // Line 1: Software name + version + codename
    sender->sendMessage(std::string(ATOMIKPE_NAME) + " v" + ATOMIKPE_VERSION +
        " (codename: " + ATOMIKPE_CODENAME + ")");
    // Line 2: Target MCPE version + protocol
    sender->sendMessage("Target: Minecraft PE " + std::string(MINECRAFT_VERSION) +
        ", protocol " + std::to_string(PROTOCOL_VERSION));
    // Line 3: C++ standard
    sender->sendMessage("Built with: C++20 / C23");
    // Line 4: Website
    sender->sendMessage("Website: github.com/Dr1xyDev/Atomik");
    // Line 5: Online players
    auto players = Server::getInstance().playersSnapshot();
    sender->sendMessage("Online: " + std::to_string(players.size()) + "/" +
        std::to_string(Server::getInstance().maxPlayers()) + " players");
    return true;
}

} // namespace
