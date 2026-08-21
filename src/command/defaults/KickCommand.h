#pragma once
#include "command/Command.h"
namespace mcppeserver {
class KickCommand : public Command {
public:
    KickCommand() : Command("kick", "Kick a player") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
