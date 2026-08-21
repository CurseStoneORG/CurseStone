#pragma once
#include "command/Command.h"
namespace mcppeserver {
class DeopCommand : public Command {
public:
    DeopCommand() : Command("deop", "Revoke operator rights") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
