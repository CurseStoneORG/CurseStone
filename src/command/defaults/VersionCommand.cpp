#include "VersionCommand.h"
#include "Server.h"
#include "main.h"
#include "utils/TextColor.h"
#include <string>

namespace cenisyspe {

bool VersionCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage(TC::GOLD + std::string(CENISYSPE_NAME) + " v" + CENISYSPE_VERSION +
        TC::YELLOW + " (codename: " + TC::WHITE + CENISYSPE_CODENAME + TC::YELLOW + ")" + TC::RESET);
    sender->sendMessage(TC::AQUA + "Target: " + TC::WHITE + "Minecraft PE " + MINECRAFT_VERSION +
        TC::GRAY + ", protocol " + std::to_string(PROTOCOL_VERSION) + TC::RESET);
    sender->sendMessage(TC::BLUE + "Built with: " + TC::WHITE + "C++20 / C23" + TC::RESET);
    sender->sendMessage(TC::LIGHT_PURPLE + "Website: " + TC::WHITE + "https://github.com/Dr1xyDev/MyCenisys" + TC::RESET);
    auto players = Server::getInstance().playersSnapshot();
    sender->sendMessage(TC::GREEN + "Online: " + TC::WHITE + std::to_string(players.size()) + "/" +
        std::to_string(Server::getInstance().maxPlayers()) + " players" + TC::RESET);
    return true;
}

} // namespace
