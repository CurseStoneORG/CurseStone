#pragma once
#include "command/Command.h"
#include <string>
namespace mcppeserver {
class TimeCommand : public Command {
public:
    TimeCommand() : Command("time", "Set/quer time") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
