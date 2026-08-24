#pragma once
#include "command/Command.h"
#include <string>
namespace atomikpe {
class ListCommand : public Command {
public:
    ListCommand() : Command("list", "List online players") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
