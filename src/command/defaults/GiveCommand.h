#pragma once
#include "command/Command.h"
namespace mcppeserver {
class GiveCommand : public Command {
public:
    GiveCommand() : Command("give", "Give items to a player") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
