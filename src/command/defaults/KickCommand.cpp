#include "KickCommand.h"
#include "Server.h"
namespace mcppeserver {
bool KickCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("kick: not implemented yet");
    return true;
}
} // namespace
