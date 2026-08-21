#include "TeleportCommand.h"
#include "Server.h"
namespace mcppeserver {
bool TeleportCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("tp: not implemented yet");
    return true;
}
} // namespace
