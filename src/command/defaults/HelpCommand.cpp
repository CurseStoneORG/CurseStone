#include "HelpCommand.h"
#include "Server.h"
namespace mcppeserver {
bool HelpCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("help: not implemented yet");
    return true;
}
} // namespace
