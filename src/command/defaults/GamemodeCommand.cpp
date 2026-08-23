#include "GamemodeCommand.h"
#include "Server.h"
namespace mcppeserver {
bool GamemodeCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("gamemode: not implemented yet");
    return true;
}
} // namespace
