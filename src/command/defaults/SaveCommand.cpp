#include "SaveCommand.h"
#include "Server.h"
#include "level/Level.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool SaveCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    auto* lvl = Server::getInstance().getDefaultLevel();
    if (lvl) { int n = lvl->save(); sender->sendMessage(TC::GREEN + "Saved world (" + std::to_string(n) + " chunks)" + TC::RESET); }
    else sender->sendMessage(TC::RED + "No world to save" + TC::RESET);
    return true;
}
} // namespace
