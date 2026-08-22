#pragma once
#include "command/Command.h"
namespace mcppeserver {
class StopCommand : public Command {
public:
    StopCommand() : Command("stop", "Stop the server") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
