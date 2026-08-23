#include "SaveCommand.h"
#include "Server.h"
namespace mcppeserver {
bool SaveCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("save-all: not implemented yet");
    return true;
}
} // namespace
