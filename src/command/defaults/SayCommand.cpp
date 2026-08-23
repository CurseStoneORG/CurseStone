#include "SayCommand.h"
#include "Server.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool SayCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage(TC::RED + "Usage: /say <message>" + TC::RESET); return true; }
    std::string msg;
    for (size_t i = 0; i < args.size(); ++i) { if (i > 0) msg += " "; msg += args[i]; }
    Server::getInstance().broadcastMessage(TC::LIGHT_PURPLE + "[" + sender->getName() + "] " + TC::WHITE + msg + TC::RESET);
    return true;
}
} // namespace
