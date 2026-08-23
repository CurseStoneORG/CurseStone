#include "SayCommand.h"
#include "Server.h"
namespace mcppeserver {
bool SayCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    if (args.empty()) { sender->sendMessage("\xc2\xa7cUsage: /say <message>"); return true; }
    std::string msg;
    for (size_t i = 0; i < args.size(); ++i) { if (i > 0) msg += " "; msg += args[i]; }
    Server::getInstance().broadcastMessage("\xc2\xa7d[" + sender->getName() + "] " + msg);
    return true;
}
} // namespace
