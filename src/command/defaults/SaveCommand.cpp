#include "SaveCommand.h"
#include "Server.h"
#include "level/Level.h"

namespace mcppeserver {
bool SaveCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    auto* lvl = Server::getInstance().getDefaultLevel();
    if (lvl) {
        int n = lvl->save();
        sender->sendMessage("§aSaved world (" + std::to_string(n) + " chunks)");
    } else {
        sender->sendMessage("§cNo world to save");
    }
    return true;
}
} // namespace
