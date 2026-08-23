#include "GiveCommand.h"
#include "Server.h"
namespace mcppeserver {
bool GiveCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage("/give is not implemented yet");
    return true;
}
} // namespace
