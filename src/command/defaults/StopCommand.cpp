#include "StopCommand.h"
#include "Server.h"
namespace cursestone {
bool StopCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>&) {
    sender->sendMessage("Stopping the server...");
    Server::getInstance().shutdown();
    return true;
}
} // namespace
