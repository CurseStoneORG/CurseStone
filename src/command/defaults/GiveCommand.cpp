#include "GiveCommand.h"
#include "Server.h"
namespace atomikpe {
bool GiveCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage("/give is not implemented yet");
    return true;
}
} // namespace
