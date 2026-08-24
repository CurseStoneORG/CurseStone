#pragma once
#include "command/Command.h"
#include <string>
namespace atomikpe {
class HelpCommand : public Command {
public:
    HelpCommand(const std::string& name = "help") : Command(name, "Show available commands") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
