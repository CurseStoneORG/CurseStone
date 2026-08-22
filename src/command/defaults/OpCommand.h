#pragma once
#include "command/Command.h"
#include <string>
namespace mcppeserver {
class OpCommand : public Command {
public:
    OpCommand() : Command("op", "Grant operator rights") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
