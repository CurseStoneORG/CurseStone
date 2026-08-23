#include "ListCommand.h"
#include "Server.h"
#include "Player.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool ListCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>&) {
    auto& players = Server::getInstance().players();
    sender->sendMessage(TC::YELLOW + "There are " + TC::WHITE + std::to_string(players.size()) +
        TC::GRAY + "/" + TC::WHITE + std::to_string(Server::getInstance().maxPlayers()) +
        TC::YELLOW + " players online:" + TC::RESET);
    for (auto& p : players) {
        if (p) sender->sendMessage(TC::GREEN + "  - " + p->getName() + TC::RESET);
    }
    return true;
}
} // namespace
