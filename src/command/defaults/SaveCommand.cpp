#include "SaveCommand.h"
#include "Server.h"
#include "level/Level.h"
namespace mcppeserver {
bool SaveCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    auto* lvl = Server::getInstance().getDefaultLevel();
    if (lvl) { int n = lvl->save(); sender->sendMessage("Saved world (" + std::to_string(n) + " chunks)"); }
    else sender->sendMessage("No world to save");
    return true;
}
} // namespace
