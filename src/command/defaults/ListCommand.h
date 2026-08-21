#pragma once
#include "command/Command.h"
namespace mcppeserver {
class ListCommand : public Command {
public:
    ListCommand() : Command("list", "List online players") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
