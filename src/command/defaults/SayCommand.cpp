#include "SayCommand.h"
#include "Server.h"
namespace cursestone {
bool SayCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("Usage: /say <message>"); return true; }
    std::string msg;
    for (size_t i = 0; i < args.size(); ++i) { if (i > 0) msg += " "; msg += args[i]; }
    Server::getInstance().broadcastMessage("[" + sender->getName() + "] " + msg);
    return true;
}
} // namespace
