#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class TimeCommand : public Command {
public:
    TimeCommand() : Command("time", "Set/quer time", "", "cursestone.command.time") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
