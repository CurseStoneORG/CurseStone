#include "ListCommand.h"
#include "Server.h"
#include "Player.h"

namespace mcppeserver {
bool ListCommand::execute(CommandSender* sender, const std::string& /*label*/,
                          const std::vector<std::string>& /*args*/) {
    auto& players = Server::getInstance().players();
    sender->sendMessage("§eThere are " + std::to_string(players.size()) +
                        "/" + std::to_string(Server::getInstance().maxPlayers()) + " players online:");
    for (auto& p : players) {
        if (p) sender->sendMessage("  - " + p->getName());
    }
    return true;
}
} // namespace
