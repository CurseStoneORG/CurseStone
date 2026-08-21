#include "ListCommand.h"
#include "Server.h"
#include "Player.h"
namespace mcppeserver {
bool ListCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>&) {
    auto& players = Server::getInstance().players();
    sender->sendMessage("Players online: " + std::to_string(players.size()));
    for (auto& p : players) sender->sendMessage("  - " + p->getName());
    return true;
}
} // namespace
