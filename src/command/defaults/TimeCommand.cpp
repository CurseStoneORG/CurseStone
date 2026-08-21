#include "TimeCommand.h"
#include "Server.h"
namespace mcppeserver {
bool TimeCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("time: not implemented yet");
    return true;
}
} // namespace
