#pragma once
#include "command/Command.h"
#include <string>
namespace mcppeserver {
class TeleportCommand : public Command {
public:
    TeleportCommand() : Command("tp", "Teleport a player") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
