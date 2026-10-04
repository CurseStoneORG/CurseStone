#include "SaveCommand.h"
#include "Server.h"
#include "world/World.h"
namespace cursestone {
bool SaveCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    auto* lvl = Server::getInstance().getDefaultWorld();
    if (lvl) { int n = lvl->save(); sender->sendMessage("Saved world (" + std::to_string(n) + " chunks)"); }
    else sender->sendMessage("No world to save");
    return true;
}
} // namespace
