#include "OpCommand.h"
#include "Server.h"
namespace mcppeserver {
bool OpCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("op: not implemented yet");
    return true;
}
} // namespace
