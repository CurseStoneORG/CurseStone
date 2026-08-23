#include "GiveCommand.h"
#include "Server.h"
namespace mcppeserver {
bool GiveCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("§c/give is not implemented yet");
    return true;
}
} // namespace
