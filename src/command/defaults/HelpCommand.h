#pragma once
#include "command/Command.h"
#include <string>
namespace mcppeserver {
class HelpCommand : public Command {
public:
    HelpCommand() : Command("help", "Show available commands") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
