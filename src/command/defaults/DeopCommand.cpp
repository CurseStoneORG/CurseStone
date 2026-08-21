#include "DeopCommand.h"
#include "Server.h"
namespace mcppeserver {
bool DeopCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("deop: not implemented yet");
    return true;
}
} // namespace
