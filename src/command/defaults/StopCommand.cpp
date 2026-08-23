#include "StopCommand.h"
#include "Server.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool StopCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>&) {
    sender->sendMessage(TC::YELLOW + "Stopping the server..." + TC::RESET);
    Server::getInstance().shutdown();
    return true;
}
} // namespace
