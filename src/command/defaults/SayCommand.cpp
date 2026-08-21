#include "SayCommand.h"
#include "Server.h"
#include <sstream>
namespace mcppeserver {
bool SayCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>& args) {
    std::ostringstream ss;
    ss << "[" << sender->getName() << "] ";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) ss << " ";
        ss << args[i];
    }
    Server::getInstance().broadcastMessage(ss.str());
    return true;
}
} // namespace
